/**
 * @file tests/unit/test_nvenc_version.cpp
 * @brief Tests for runtime NVENC SDK version selection.
 */

// standard includes
#include <array>
#include <cstdint>

// lib includes
#include <gtest/gtest.h>

// local includes
#include "src/nvenc/nvenc_version.h"

namespace {

  /**
   * @brief Expected SDK selection for a driver API version.
   */
  struct nvenc_version_test_case {
    std::uint32_t max_version;  ///< Maximum API version reported by the driver.
    nvenc::nvenc_sdk_version expected;  ///< SDK implementation Sunshine should select.
  };

  TEST(NvencVersionTest, DecodesPackedDriverVersion) {
    EXPECT_EQ(nvenc::decode_nvenc_driver_version((11U << 4U) | 0U), 1100U);
    EXPECT_EQ(nvenc::decode_nvenc_driver_version((13U << 4U) | 1U), 1301U);
  }

  TEST(NvencVersionTest, SelectsNewestCompatibleSdk) {
    using enum nvenc::nvenc_sdk_version;
    constexpr std::array test_cases {
      nvenc_version_test_case {1000U, unsupported},
      nvenc_version_test_case {1099U, unsupported},
      nvenc_version_test_case {1100U, sdk_11_0},
      nvenc_version_test_case {1101U, sdk_11_0},
      nvenc_version_test_case {1199U, sdk_11_0},
      nvenc_version_test_case {1200U, sdk_12_0},
      nvenc_version_test_case {1201U, sdk_12_0},
      nvenc_version_test_case {1299U, sdk_12_0},
      nvenc_version_test_case {1300U, sdk_13_0},
      nvenc_version_test_case {1301U, sdk_13_0},
      nvenc_version_test_case {1400U, sdk_13_0},
    };

    for (const auto &[max_version, expected] : test_cases) {
      EXPECT_EQ(nvenc::select_nvenc_sdk_version(max_version), expected);
    }
  }

}  // namespace
