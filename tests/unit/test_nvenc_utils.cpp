/**
 * @file tests/unit/test_nvenc_utils.cpp
 * @brief Tests for Sunshine pixel format conversion in the NVENC SDK 13.1 implementation.
 */

#ifdef _WIN32
  // standard includes
  #include <array>
  #include <utility>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/nvenc/nvenc_utils.h"

TEST(NvencFormatMappingTest, MapsSupportedFormatsAndRejectsUnsupportedOnes) {
  using enum platf::pix_fmt_e;
  using enum nvenc_1301::NV_ENC_BUFFER_FORMAT;
  const std::array cases {
    std::pair {nv12, NV_ENC_BUFFER_FORMAT_NV12},
    std::pair {p010, NV_ENC_BUFFER_FORMAT_YUV420_10BIT},
    std::pair {ayuv, NV_ENC_BUFFER_FORMAT_AYUV},
    std::pair {yuv444p, NV_ENC_BUFFER_FORMAT_YUV444},
    std::pair {yuv444p16, NV_ENC_BUFFER_FORMAT_YUV444_10BIT},
    std::pair {unknown, NV_ENC_BUFFER_FORMAT_UNDEFINED},
    std::pair {y410, NV_ENC_BUFFER_FORMAT_UNDEFINED},
    std::pair {yuv420p, NV_ENC_BUFFER_FORMAT_UNDEFINED},
    std::pair {yuv420p10, NV_ENC_BUFFER_FORMAT_UNDEFINED},
  };
  for (const auto &[format, expected] : cases) {
    EXPECT_EQ(nvenc_1301::nvenc_format_from_sunshine_format(format), expected) << std::to_underlying(format);
  }
}
#endif
