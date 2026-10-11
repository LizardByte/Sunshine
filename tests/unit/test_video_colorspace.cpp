/**
 * @file tests/unit/test_video_colorspace.cpp
 * @brief Tests for client colorspace selection and RGB-to-YUV conversion vectors.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <tuple>
#include <utility>

// local includes
#include <src/video.h>

TEST(VideoColorspaceTest, DescribesEveryColorspaceAndUnknownValues) {
  using enum video::colorspace_e;
  const std::array cases {
    std::pair {rec601, "SDR (Rec. 601)"},
    std::pair {rec709, "SDR (Rec. 709)"},
    std::pair {bt2020sdr, "SDR (Rec. 2020)"},
    std::pair {bt2020, "HDR (Rec. 2020 + SMPTE 2084 PQ)"},
    std::pair {static_cast<video::colorspace_e>(-1), "unknown"},
  };
  for (const auto &[colorspace, expected] : cases) {
    EXPECT_STREQ(video::colorspace_to_string(colorspace), expected);
  }
}

TEST(VideoColorspaceTest, SelectsClientColorspaceAndPreservesFallbacks) {
  using enum video::colorspace_e;
  const std::array cases {
    std::tuple {0, 0, false, rec601, 8U},
    std::tuple {2, 0, false, rec709, 8U},
    std::tuple {4, 1, false, bt2020sdr, 10U},
    std::tuple {4, 0, false, rec709, 8U},
    std::tuple {6, 0, false, rec709, 8U},
    std::tuple {0, 1, true, bt2020, 10U},
    std::tuple {0, 1, false, rec601, 10U},
    std::tuple {0, 0, true, rec601, 8U},
    std::tuple {2, 2, false, rec709, 10U},
  };

  for (const auto &[mode, dynamic_range, hdr_display, expected, bit_depth] : cases) {
    for (bool full_range : {false, true}) {
      video::config_t config {};
      config.encoderCscMode = mode | static_cast<int>(full_range);
      config.dynamicRange = dynamic_range;
      const auto colorspace = video::colorspace_from_client_config(config, hdr_display);
      EXPECT_EQ(colorspace.colorspace, expected) << mode;
      EXPECT_EQ(colorspace.bit_depth, bit_depth);
      EXPECT_EQ(colorspace.full_range, full_range);
    }
  }
}

TEST(VideoColorspaceTest, ConvertsBlackWhiteAndPrimaryRedInEveryOutputRange) {
  using enum video::colorspace_e;
  const std::array cases {
    std::pair {rec601, 0.299f},
    std::pair {rec709, 0.2126f},
    std::pair {bt2020sdr, 0.2627f},
    std::pair {bt2020, 0.2627f},
    std::pair {static_cast<video::colorspace_e>(-1), 0.2126f},
  };

  constexpr std::array output_ranges {
    std::pair {false, false},
    std::pair {false, true},
    std::pair {true, false},
    std::pair {true, true},
  };
  for (const auto &[colorspace, red_luma] : cases) {
    for (unsigned bit_depth : {8U, 10U}) {
      for (const auto &[full_range, unorm_output] : output_ranges) {
        const auto *vectors = video::color_vectors_from_colorspace({colorspace, full_range, bit_depth}, unorm_output);
        ASSERT_NE(vectors, nullptr);
        const float max_sample = (1U << bit_depth) - 1;
        const float scale = 1U << (bit_depth - 8);
        const float divisor = unorm_output ? max_sample : 1.0f;
        const float rounding = unorm_output ? 0.0f : 0.5f;
        const float black = (full_range ? 0.0f : 16.0f * scale) / divisor + rounding;
        const float white = (full_range ? max_sample : 235.0f * scale) / divisor + rounding;
        const float neutral_chroma = (128.0f * scale) / divisor + rounding;

        EXPECT_FLOAT_EQ(vectors->color_vec_y[3], black);
        EXPECT_NEAR(vectors->color_vec_y[0] + vectors->color_vec_y[1] + vectors->color_vec_y[2] + vectors->color_vec_y[3], white, 0.0001f);
        EXPECT_NEAR(vectors->color_vec_y[0], red_luma * (white - black), 0.0001f);
        EXPECT_FLOAT_EQ(vectors->color_vec_u[3], neutral_chroma);
        EXPECT_FLOAT_EQ(vectors->color_vec_v[3], neutral_chroma);
        EXPECT_NEAR(vectors->color_vec_u[0] + vectors->color_vec_u[1] + vectors->color_vec_u[2], 0.0f, 0.0001f);
        EXPECT_NEAR(vectors->color_vec_v[0] + vectors->color_vec_v[1] + vectors->color_vec_v[2], 0.0f, 0.0001f);
      }
    }
  }
}
