/**
 * @file tests/unit/platform/windows/test_nvprefs_undo_data.cpp
 * @brief Regression coverage for absent and zero NVIDIA preference restore values.
 */

#ifdef _WIN32

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/platform/windows/nvprefs/undo_data.h"

/**
 * @brief Serialization preserves the distinction between zero and an absent driver setting.
 */
TEST(NvPrefsUndoDataTest, PreservesZeroAndAbsentRestoreValues) {
  for (const auto value : {std::optional<std::uint32_t> {}, std::optional<std::uint32_t> {0}, std::optional<std::uint32_t> {7}}) {
    nvprefs::undo_data_t original;
    original.set_opengl_swapchain(1, value);
    const auto serialized = original.write();
    nvprefs::undo_data_t restored;
    restored.read(std::vector<char> {serialized.begin(), serialized.end()});
    const auto setting = restored.get_opengl_swapchain();
    ASSERT_TRUE(setting.has_value());
    EXPECT_EQ(setting->our_value, 1);
    EXPECT_EQ(setting->undo_value, value);
  }
}

#endif
