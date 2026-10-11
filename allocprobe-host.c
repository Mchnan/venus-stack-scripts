/*
 * allocprobe-host — pin the remaining MVK quirks the vkr shim must cover:
 *  1. vkAllocateMemory with [Metal import -> ExportMemoryAllocateInfo(DMA_BUF)]
 *     (the exact chain the vkr darwin metal path hands to MoltenVK when the
 *      guest requests dma-buf exportable memory).
 *  2. GetPhysicalDeviceImageFormatProperties2 with a ycbcr OUTPUT pNext
 *     (zink always attaches it when KHR_sampler_ycbcr_conversion is on).
 *  3. ATTACHMENT_FEEDBACK_LOOP usage in the format query.
 */
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

static const char *rs(VkResult r)
{
   switch (r) {
   case VK_SUCCESS: return "SUCCESS";
   case VK_ERROR_FORMAT_NOT_SUPPORTED: return "FORMAT_NOT_SUPPORTED";
   case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "INVALID_EXTERNAL_HANDLE";
   case VK_ERROR_OUT_OF_HOST_MEMORY: return "OUT_OF_HOST_MEMORY";
   case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "OUT_OF_DEVICE_MEMORY";
   default: return "OTHER";
   }
}

int main(void)
{
   const char *port_ext[] = {"VK_KHR_portability_enumeration"};
   VkInstanceCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
      .enabledExtensionCount = 1, .ppEnabledExtensionNames = port_ext };
   VkInstance inst;
   if (vkCreateInstance(&ci, NULL, &inst) != VK_SUCCESS) { printf("instance FAIL\n"); return 1; }
   uint32_t nd = 0;
   vkEnumeratePhysicalDevices(inst, &nd, NULL);
   VkPhysicalDevice devs[8];
   vkEnumeratePhysicalDevices(inst, &nd, devs);
   VkPhysicalDevice phy = devs[0];

   int have_fb_loop = 0, have_ycbcr = 0;
   uint32_t ne = 0;
   vkEnumerateDeviceExtensionProperties(phy, NULL, &ne, NULL);
   VkExtensionProperties de[512];
   vkEnumerateDeviceExtensionProperties(phy, NULL, &ne, de);
   for (uint32_t i = 0; i < ne; i++) {
      if (!strcmp(de[i].extensionName, "VK_EXT_attachment_feedback_loop_layout")) have_fb_loop = 1;
      if (!strcmp(de[i].extensionName, "VK_KHR_sampler_ycbcr_conversion")) have_ycbcr = 1;
   }
   printf("ext: attachment_feedback_loop_layout=%d ycbcr=%d\n", have_fb_loop, have_ycbcr);

   const VkImageUsageFlags base_usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   /* 2+3: query variants */
   {
      VkSamplerYcbcrConversionImageFormatProperties ycbcr = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES };
      VkPhysicalDeviceImageFormatInfo2 info = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
         .format = VK_FORMAT_B8G8R8A8_UNORM,
         .type = VK_IMAGE_TYPE_2D,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = base_usage,
      };
      VkImageFormatProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
      if (have_ycbcr) p2.pNext = &ycbcr;
      printf("query base + ycbcr_out_pNext -> %s\n",
             rs(vkGetPhysicalDeviceImageFormatProperties2(phy, &info, &p2)));

      VkImageFormatProperties2 p3 = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
      VkPhysicalDeviceImageFormatInfo2 info3 = info;
      info3.usage = base_usage | VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
      printf("query +FEEDBACK_LOOP usage (no pNext) -> %s\n",
             rs(vkGetPhysicalDeviceImageFormatProperties2(phy, &info3, &p3)));
   }

   /* 1: allocate with the exact vkr darwin metal chain */
   {
      float prio = 1.0f;
      VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                      .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
      VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                 .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
      VkDevice dev;
      if (vkCreateDevice(phy, &dci, NULL, &dev) != VK_SUCCESS) { printf("device FAIL\n"); return 1; }

      VkPhysicalDeviceMemoryProperties mp;
      vkGetPhysicalDeviceMemoryProperties(phy, &mp);
      int mem_idx = -1;
      for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
         if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            mem_idx = (int)i;
            break;
         }
      }
      printf("host-visible memory type: %d\n", mem_idx);

      VkExportMemoryAllocateInfo emai = {
         .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
         .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
      /* standalone emai (what MVK would see if the metal import weren't there) */
      VkMemoryAllocateInfo mai1 = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                    .allocationSize = 1 << 20,
                                    .memoryTypeIndex = (uint32_t)mem_idx,
                                    .pNext = &emai };
      VkDeviceMemory mem1;
      VkResult r = vkAllocateMemory(dev, &mai1, NULL, &mem1);
      printf("vkAllocateMemory +emai(DMA_BUF) only            -> %s\n", rs(r));
      if (r == VK_SUCCESS) vkFreeMemory(dev, mem1, NULL);

      /* with a real MTLBuffer is not doable here; NULL handle tests the
       * validation path, which is what matters for the emai question. */
      vkDestroyDevice(dev, NULL);
   }

   vkDestroyInstance(inst, NULL);
   return 0;
}
