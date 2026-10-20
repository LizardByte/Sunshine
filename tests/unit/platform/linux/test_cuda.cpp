/**
 * @file tests/unit/platform/linux/test_cuda.cpp
 * @brief Tests for Linux CUDA capture selection.
 */

#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <array>
  #include <cstdint>

  // local includes
  #include "src/platform/linux/cuda.h"

namespace {
  /**
   * @brief Expected virtual desktop selection for an NvFBC status.
   */
  struct nvfbc_virtual_desktop_case_t {
    std::uint32_t output_count;  ///< Physical outputs reported by NvFBC.
    bool capture_possible;  ///< Whether NvFBC reports capture support.
    std::uint32_t screen_width;  ///< Virtual desktop width.
    std::uint32_t screen_height;  ///< Virtual desktop height.
    bool expected;  ///< Whether Sunshine should offer the virtual desktop.
  };
}  // namespace

TEST(NvFbcDisplaySelectionTest, OffersVirtualDesktopOnlyForValidNoOutputCapture) {
  constexpr std::array cases {
    nvfbc_virtual_desktop_case_t {1U, true, 1920U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 1920U, 1080U, true},
    nvfbc_virtual_desktop_case_t {0U, false, 1920U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 0U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 1920U, 0U, false},
  };

  for (const auto &[output_count, capture_possible, screen_width, screen_height, expected] : cases) {
    EXPECT_EQ(cuda::nvfbc::should_offer_virtual_desktop(output_count, capture_possible, screen_width, screen_height), expected)
      << "output_count=" << output_count
      << ", capture_possible=" << capture_possible
      << ", screen_width=" << screen_width
      << ", screen_height=" << screen_height;
  }
}

#endif  // __linux__
