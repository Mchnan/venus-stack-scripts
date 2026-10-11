/*
 * fmtprobe-host — refine MoltenVK's quirks around
 * vkGetPhysicalDeviceImageFormatProperties2 pNext handling, to pin the
 * exact contract a vkr shim can honestly synthesize.
 */
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

static const char *rs(VkResult r)
{
   switch (r) {
   case VK_SUCCESS: return "SUCCESS";
   case VK_ERROR_FORMAT_NOT_SUPPORTED: return "FORMAT_NOT_SUPPORTED";
   case VK_ERROR_OUT_OF_HOST_MEMORY: return "OUT_OF_HOST_MEMORY";
   default: return "OTHER";
   }
}

static VkResult query(VkPhysicalDevice phy, VkImageTiling tiling, VkImageUsageFlags usage,
                      VkImageCreateFlags flags, int nviews, const VkFormat *views,
                      int with_hic, int *hic_optimal, int *hic_identical)
{
   VkImageFormatListCreateInfo fl = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO };
   if (nviews > 0) { fl.viewFormatCount = nviews; fl.pViewFormats = views; }

   VkSamplerYcbcrConversionImageFormatProperties ycbcr = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES };
   VkHostImageCopyDevicePerformanceQueryEXT hic = {
      .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT };

   VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = tiling,
      .usage = usage,
      .flags = flags,
      .pNext = nviews > 0 ? &fl : NULL,
   };
   VkBaseOutStructure *out_head = NULL;
   if (with_hic) { hic.pNext = out_head; out_head = (VkBaseOutStructure *)&hic; }
   VkImageFormatProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = out_head };

   VkResult ret = vkGetPhysicalDeviceImageFormatProperties2(phy, &info, &p2);
   if (hic_optimal) *hic_optimal = hic.optimalDeviceAccess;
   if (hic_identical) *hic_identical = hic.identicalMemoryLayout;
   return ret;
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

   const VkImageUsageFlags base_usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   /* 1. core format properties vs pNext-3 format properties */
   {
      VkFormatProperties core;
      vkGetPhysicalDeviceFormatProperties(phy, VK_FORMAT_B8G8R8A8_UNORM, &core);
      printf("core props optimal=0x%llx linear=0x%llx\n",
             (unsigned long long)core.optimalTilingFeatures,
             (unsigned long long)core.linearTilingFeatures);

      VkFormatProperties3 fp3 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
      VkFormatProperties2 fp2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &fp3 };
      vkGetPhysicalDeviceFormatProperties2(phy, VK_FORMAT_B8G8R8A8_UNORM, &fp2);
      printf("props2(pNext3) optimal3=0x%llx  props2.core optimal=0x%llx\n",
             (unsigned long long)fp3.optimalTilingFeatures,
             (unsigned long long)fp2.formatProperties.optimalTilingFeatures);
   }

   /* 2. features2 core + hostImageCopy chain */
   {
      VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
      vkGetPhysicalDeviceFeatures2(phy, &f2);
      printf("features2 core: geometryShader=%d tessellationShader=%d\n",
             f2.features.geometryShader, f2.features.tessellationShader);
      VkPhysicalDeviceHostImageCopyFeaturesEXT hicf = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT };
      f2.pNext = &hicf;
      vkGetPhysicalDeviceFeatures2(phy, &f2);
      printf("features2 +hic pNext: hostImageCopy=%d\n", hicf.hostImageCopy);
   }

   /* 3. fmtlist variants */
   const VkFormat views1[1] = { VK_FORMAT_B8G8R8A8_UNORM };
   const VkFormat views2[2] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB };
   int h1, h2;
   printf("fmtlist x1           -> %s\n", rs(query(phy, VK_IMAGE_TILING_OPTIMAL, base_usage, 0, 1, views1, 0, NULL, NULL)));
   printf("fmtlist x2(srgb)     -> %s\n", rs(query(phy, VK_IMAGE_TILING_OPTIMAL, base_usage, 0, 2, views2, 0, NULL, NULL)));
   printf("no INPUT_ATTACHMENT, fmtlist x1 -> %s\n",
          rs(query(phy, VK_IMAGE_TILING_OPTIMAL,
                   base_usage & ~VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 0, 1, views1, 0, NULL, NULL)));
   printf("MUTABLE fmtlist x1   -> %s\n", rs(query(phy, VK_IMAGE_TILING_OPTIMAL, base_usage,
                                                  VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, 1, views1, 0, NULL, NULL)));

   /* 4. HOST_TRANSFER usage variants */
   printf("host_xfer no pNext   -> %s (hic optimal=%d identical=%d)\n",
          rs(query(phy, VK_IMAGE_TILING_OPTIMAL, base_usage | VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT, 0, 0, NULL, 1, &h1, &h2)),
          h1, h2);

   /* 5. does vkCreateImage itself work with fmtlist / with MUTABLE? */
   {
      float prio = 1.0f;
      VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                      .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
      VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                 .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
      VkDevice dev;
      if (vkCreateDevice(phy, &dci, NULL, &dev) != VK_SUCCESS) { printf("device FAIL\n"); return 1; }

      const VkFormat views[2] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB };
      VkImageFormatListCreateInfo fl = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
                                         .viewFormatCount = 2, .pViewFormats = views };
      VkImageCreateInfo ici = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .pNext = &fl,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = VK_FORMAT_B8G8R8A8_UNORM,
         .extent = { 64, 64, 1 },
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = base_usage,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      VkImage img;
      VkResult r = vkCreateImage(dev, &ici, NULL, &img);
      printf("vkCreateImage +fmtlist(base usage, no MUT flag) -> %s\n", rs(r));
      if (r == VK_SUCCESS) vkDestroyImage(dev, img, NULL);

      ici.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
      r = vkCreateImage(dev, &ici, NULL, &img);
      printf("vkCreateImage +fmtlist+MUTABLE -> %s\n", rs(r));
      if (r == VK_SUCCESS) vkDestroyImage(dev, img, NULL);

      ici.pNext = NULL;
      ici.usage = base_usage | VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT;
      r = vkCreateImage(dev, &ici, NULL, &img);
      printf("vkCreateImage +HOST_TRANSFER(no fmtlist) -> %s\n", rs(r));
      if (r == VK_SUCCESS) vkDestroyImage(dev, img, NULL);

      vkDestroyDevice(dev, NULL);
   }

   vkDestroyInstance(inst, NULL);
   return 0;
}
