/*
 * metal-shm-test — replicate vkr's darwin mtl_shm flow and verify that
 * GPU compute writes land in the shm pages a second mmap can observe.
 *
 *  1. shm_open anonymous-style region + MAP_SHARED mapping
 *  2. wrap it with newBufferWithBytesNoCopy (StorageModeShared)
 *  3. vkAllocateMemory importing that MTLBuffer (ImportMemoryMetalHandleEXT)
 *  4. bind a storage buffer, dispatch the vkprobe compute shader
 *  5. vkWaitForFences, then read back BOTH via vkMapMemory and via a
 *     fresh mmap of the same shm object.
 *
 * PASS: both readbacks show 0xc0de0000..3.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#import <Metal/Metal.h>

#include <vulkan/vulkan.h>
#include "vulkan_metal.h"

static const uint32_t MAGIC0 = 0xc0de0000u;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);                     \
            return 1;                                                          \
        }                                                                      \
        printf("ok: %s\n", msg);                                               \
    } while (0)

int main(void)
{
    const size_t SIZE = 4096;

    /* --- 1. shm region (mirrors vkr_mtl_shm_alloc) --- */
    char name[64];
    snprintf(name, sizeof(name), "/metal-shm-test-%d", (int)getpid());
    shm_unlink(name);
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0, "shm_open");
    CHECK(ftruncate(fd, SIZE) == 0, "ftruncate");
    uint32_t *shm_view1 = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(shm_view1 != MAP_FAILED, "mmap view1");
    memset(shm_view1, 0, SIZE);

    /* --- Vulkan setup --- */
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "metal-shm-test",
                              .apiVersion = VK_API_VERSION_1_2 };
    const char *portability_ext = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
                                 .pApplicationInfo = &app,
                                 .enabledExtensionCount = 1,
                                 .ppEnabledExtensionNames = &portability_ext };
    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst) == VK_SUCCESS, "vkCreateInstance");

    uint32_t nd = 1;
    VkPhysicalDevice phy;
    CHECK(vkEnumeratePhysicalDevices(inst, &nd, &phy) == VK_SUCCESS && nd == 1,
          "enumerate MoltenVK");

    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phy, &nq, NULL);
    VkQueueFamilyProperties qp[8];
    vkGetPhysicalDeviceQueueFamilyProperties(phy, &nq, qp);
    int qf = -1;
    for (uint32_t i = 0; i < nq; i++)
        if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qf = (int)i; break; }
    CHECK(qf >= 0, "compute queue family");

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                    .queueFamilyIndex = (uint32_t)qf,
                                    .queueCount = 1,
                                    .pQueuePriorities = &prio };
    const char *dev_exts[] = { "VK_EXT_external_memory_metal", "VK_EXT_metal_objects" };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                               .queueCreateInfoCount = 1,
                               .pQueueCreateInfos = &qci,
                               .enabledExtensionCount = 2,
                               .ppEnabledExtensionNames = dev_exts };
    VkDevice dev;
    CHECK(vkCreateDevice(phy, &dci, NULL, &dev) == VK_SUCCESS, "vkCreateDevice");

    VkQueue queue;
    vkGetDeviceQueue(dev, (uint32_t)qf, 0, &queue);

    /* --- 2. wrap the shm pages as an MTLBuffer --- */
    VkExportMetalDeviceInfoEXT mdev_info = { .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT };
    VkExportMetalObjectsInfoEXT export_info = { .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
                                                .pNext = &mdev_info };
    PFN_vkExportMetalObjectsEXT pfnExport =
        (PFN_vkExportMetalObjectsEXT)vkGetDeviceProcAddr(dev, "vkExportMetalObjectsEXT");
    CHECK(pfnExport, "vkExportMetalObjectsEXT available");
    pfnExport(dev, &export_info);
    CHECK(mdev_info.mtlDevice != NULL, "export MTLDevice");
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)mdev_info.mtlDevice;

    id<MTLBuffer> mtlBuffer = [mtlDevice newBufferWithBytesNoCopy:shm_view1
                                                           length:SIZE
                                                          options:MTLResourceStorageModeShared
                                                      deallocator:nil];
    CHECK(mtlBuffer != NULL, "newBufferWithBytesNoCopy over shm");

    /* --- 3. vkAllocateMemory importing the MTLBuffer --- */
    VkImportMemoryMetalHandleInfoEXT import = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_METAL_HANDLE_INFO_EXT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLBUFFER_BIT_EXT,
        .handle = (__bridge void *)mtlBuffer,
    };
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = SIZE,
                                 .pNext = &import };
    VkMemoryRequirements mr = { .memoryTypeBits = 0xFFFFFFFFu,
                                .alignment = 1, .size = SIZE };
    /* pick a HOST_VISIBLE|HOST_COHERENT type */
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phy, &mp);
    int mem_idx = -1;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        uint32_t f = mp.memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mem_idx = (int)i; break; }
    }
    CHECK(mem_idx >= 0, "host coherent memory type");
    mai.memoryTypeIndex = (uint32_t)mem_idx;
    (void)mr;

    VkDeviceMemory mem;
    VkResult ar = vkAllocateMemory(dev, &mai, NULL, &mem);
    printf("vkAllocateMemory: %d\n", (int)ar);
    CHECK(ar == VK_SUCCESS, "import allocation");

    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = SIZE,
                               .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer buf;
    CHECK(vkCreateBuffer(dev, &bci, NULL, &buf) == VK_SUCCESS, "vkCreateBuffer");
    CHECK(vkBindBufferMemory(dev, buf, mem, 0) == VK_SUCCESS, "vkBindBufferMemory");

    /* --- 4. compute pipeline (same SPIR-V as vkprobe) --- */
    VkShaderModuleCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    static const uint32_t spv[] = {
#include "metal_test_spv.h"
    };
    sci.codeSize = sizeof(spv);
    sci.pCode = spv;
    VkShaderModule smod;
    CHECK(vkCreateShaderModule(dev, &sci, NULL, &smod) == VK_SUCCESS, "vkCreateShaderModule");

    VkDescriptorSetLayoutBinding binding = {
        .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                              .bindingCount = 1, .pBindings = &binding };
    VkDescriptorSetLayout dsl;
    CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl) == VK_SUCCESS, "dsl");
    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout playout;
    CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &playout) == VK_SUCCESS, "playout");
    VkComputePipelineCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                         .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                    .module = smod, .pName = "main" },
                                         .layout = playout };
    VkPipeline pipe;
    CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe) == VK_SUCCESS, "pipeline");

    VkDescriptorPoolSize psize = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &psize };
    VkDescriptorPool pool;
    CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &pool) == VK_SUCCESS, "pool");
    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet dset;
    CHECK(vkAllocateDescriptorSets(dev, &dsai, &dset) == VK_SUCCESS, "alloc dset");
    VkDescriptorBufferInfo dbi = { .buffer = buf, .offset = 0, .range = SIZE };
    VkWriteDescriptorSet wds = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                 .dstSet = dset, .descriptorCount = 1,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 .pBufferInfo = &dbi };
    vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

    VkCommandPoolCreateInfo cpci2 = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                      .queueFamilyIndex = (uint32_t)qf };
    VkCommandPool cpool;
    CHECK(vkCreateCommandPool(dev, &cpci2, NULL, &cpool) == VK_SUCCESS, "cmdpool");
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb) == VK_SUCCESS, "cmdbuf");
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cb, &bi) == VK_SUCCESS, "begin");
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
    vkCmdDispatch(cb, 1, 1, 1);
    CHECK(vkEndCommandBuffer(cb) == VK_SUCCESS, "end");

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHECK(vkCreateFence(dev, &fci, NULL, &fence) == VK_SUCCESS, "fence");
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &cb };
    CHECK(vkQueueSubmit(queue, 1, &si, fence) == VK_SUCCESS, "submit");
    VkResult wr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
    printf("vkWaitForFences: %d\n", (int)wr);
    CHECK(wr == VK_SUCCESS, "fence wait");

    /* --- 5. readbacks --- */
    void *mapped = NULL;
    CHECK(vkMapMemory(dev, mem, 0, SIZE, 0, &mapped) == VK_SUCCESS, "vkMapMemory");
    const uint32_t *via_map = (const uint32_t *)mapped;
    const uint32_t *via_shm = (const uint32_t *)shm_view1;


    /* independent mmap of the same shm object (what the QEMU proxy does) */
    uint32_t *shm_view2 = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(shm_view2 != MAP_FAILED, "mmap view2 (independent)");
    printf("view2[0..3]: %08x %08x %08x %08x\n", shm_view2[0], shm_view2[1], shm_view2[2], shm_view2[3]);
    int pass = 1;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t want = MAGIC0 + i;
        printf("readback[%u]: vkMap=0x%08x shm=0x%08x want=0x%08x\n", i, via_map[i], via_shm[i], want);
        if (via_map[i] != want || via_shm[i] != want)
            pass = 0;
    }
    printf(pass ? "=== METAL-SHM PASS ===\n" : "=== METAL-SHM FAIL ===\n");
    return pass ? 0 : 1;
}
