#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
int main(void) {
    const char *port_ext[] = {"VK_KHR_portability_enumeration"};
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = port_ext };
    VkInstance inst;
    if (vkCreateInstance(&ci, NULL, &inst) != VK_SUCCESS) { printf("CREATE_INSTANCE_FAIL\n"); return 1; }
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    VkExtensionProperties ext[256]; vkEnumerateInstanceExtensionProperties(NULL, &n, ext);
    int has_export_metal = 0, has_ext_mem_metal = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!strcmp(ext[i].extensionName, "VK_EXT_metal_surface")) has_export_metal = 1;
        printf("instance_ext: %s v%u\n", ext[i].extensionName, ext[i].specVersion);
    }
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(inst, &nd, NULL);
    VkPhysicalDevice devs[8]; vkEnumeratePhysicalDevices(inst, &nd, devs);
    printf("physical_devices: %u\n", nd);
    for (uint32_t d = 0; d < nd && d < 8; d++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devs[d], &p);
        printf("device[%u]: %s (Vulkan %u.%u.%u, driver %u.%u.%u)\n", d, p.deviceName,
               VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion),
               VK_API_VERSION_MAJOR(p.driverVersion), VK_API_VERSION_MINOR(p.driverVersion), VK_API_VERSION_PATCH(p.driverVersion));
        uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(devs[d], NULL, &ne, NULL);
        static VkExtensionProperties de[512]; vkEnumerateDeviceExtensionProperties(devs[d], NULL, &ne, de);
        for (uint32_t i = 0; i < ne; i++) {
            if (!strcmp(de[i].extensionName, "VK_EXT_external_memory_metal")) has_ext_mem_metal = 1;
            if (!strncmp(de[i].extensionName, "VK_EXT_external_memory", 22))
                printf("  dev_ext: %s\n", de[i].extensionName);
        }
    }
    printf("VERDICT export_metal_instance=%d external_memory_metal_device=%d\n", has_export_metal, has_ext_mem_metal);
    vkDestroyInstance(inst, NULL);
    return 0;
}
