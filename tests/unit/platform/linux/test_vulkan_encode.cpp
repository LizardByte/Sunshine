/**
 * @file tests/unit/platform/linux/test_vulkan_encode.cpp
 * @brief Tests for Vulkan encoder hardware frame configuration.
 */

#ifdef SUNSHINE_BUILD_VULKAN

  // test includes
  #include "../../../tests_common.h"

// lib includes
extern "C" {
  #include <libavutil/hwcontext.h>
  #include <libavutil/hwcontext_vulkan.h>
}

  // local includes
  #include "src/platform/linux/vulkan_encode.h"

/**
 * @brief Leave optional video encode image usage to FFmpeg's capability checks.
 */
TEST(VulkanEncodeDeviceTest, DefersVideoEncodeImageUsageUntilDeviceSupportIsKnown) {
  auto device = vk::make_avcodec_encode_device_vram(1920, 1080, 0, 0);
  ASSERT_NE(device, nullptr);

  AVVulkanFramesContext vulkan_frames {};
  AVHWFramesContext frames {};
  frames.hwctx = &vulkan_frames;

  device->init_hwframes(&frames);

  EXPECT_EQ(vulkan_frames.usage & VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR, 0);
  EXPECT_EQ(vulkan_frames.usage & VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_USAGE_STORAGE_BIT);
  EXPECT_EQ(vulkan_frames.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  EXPECT_EQ(vulkan_frames.usage & VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_USAGE_SAMPLED_BIT);
  EXPECT_EQ(vulkan_frames.tiling, VK_IMAGE_TILING_OPTIMAL);
  EXPECT_EQ(frames.initial_pool_size, 4);
}

#endif  // SUNSHINE_BUILD_VULKAN
