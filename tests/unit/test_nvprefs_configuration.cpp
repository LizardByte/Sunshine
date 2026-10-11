/**
 * @file tests/unit/test_nvprefs_configuration.cpp
 * @brief Test per-host NVIDIA profile policy.
 */
#include "../tests_common.h"
#ifdef _WIN32
  #include <src/platform/windows/nvprefs/nvprefs_common.h>

TEST(NvPrefsConfigurationTest, IndependentHostsUseOnlyTheirOwnProfilePolicy) {
  config::settings_t first;
  first.video.nv_opengl_vulkan_on_dxgi = false;
  first.video.nv_sunshine_high_power_mode = true;
  config::settings_t second;
  second.video.nv_opengl_vulkan_on_dxgi = true;
  second.video.nv_sunshine_high_power_mode = false;
  const auto first_policy = nvprefs::get_nvprefs_options(first);
  const auto second_policy = nvprefs::get_nvprefs_options(second);
  EXPECT_FALSE(first_policy.opengl_vulkan_on_dxgi);
  EXPECT_TRUE(first_policy.sunshine_high_power_mode);
  EXPECT_TRUE(second_policy.opengl_vulkan_on_dxgi);
  EXPECT_FALSE(second_policy.sunshine_high_power_mode);
  EXPECT_FALSE(nvprefs::get_nvprefs_options(first).opengl_vulkan_on_dxgi);
}
#endif
