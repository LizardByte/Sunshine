/**
 * @file tests/unit/test_video.cpp
 * @brief Test src/video.*.
 */
#include "../tests_common.h"

#include <src/config.h>
#include <src/video.h>

// SUNSHINE_FORMAT_* enumerator-value invariants are asserted next to the
// definitions in src/video.h:28-31; not duplicated here. The COUNT
// sentinel is the test-relevant invariant — it gates is_known_video_format
// and must follow PRORES.
static_assert(video::SUNSHINE_FORMAT_COUNT == video::SUNSHINE_FORMAT_PRORES + 1);

struct EncoderTest: PlatformTestSuite, testing::WithParamInterface<video::encoder_t *> {
  void SetUp() override {
    auto &encoder = *GetParam();
    if (!video::validate_encoder(encoder, false)) {
      // Encoder failed validation,
      // if it's software - fail, otherwise skip
      if (encoder.name == "software") {
        FAIL() << "Software encoder not available";
      } else {
        GTEST_SKIP() << "Encoder not available";
      }
    }
  }
};

INSTANTIATE_TEST_SUITE_P(
  EncoderVariants,
  EncoderTest,
  testing::Values(
#if !defined(__APPLE__)
    &video::nvenc,
#endif
#ifdef _WIN32
    &video::amdvce,
    &video::quicksync,
#endif
#if defined(__linux__) || defined(__FreeBSD__)
    &video::vaapi,
#endif
#ifdef __APPLE__
    &video::videotoolbox,
#endif
    &video::software
  ),
  [](const auto &info) {
    return std::string(info.param->name);
  }
);

TEST(ProResConfigTest, DefaultsDisabled) {
  EXPECT_EQ(config::video.prores_mode, 0);
  EXPECT_EQ(config::video.prores_profile, "lt");
}

TEST(ProResConfigTest, ParsesExplicitModeAndProfile) {
  auto mode = config::video.prores_mode;
  auto profile = config::video.prores_profile;

  config::apply_config({
    {"prores_mode", "1"},
    {"prores_profile", "hq"},
  });

  EXPECT_EQ(config::video.prores_mode, 1);
  EXPECT_EQ(config::video.prores_profile, "hq");

  config::video.prores_mode = mode;
  config::video.prores_profile = profile;
}

TEST(ProResConfigTest, NormalizesInvalidValues) {
  auto mode = config::video.prores_mode;
  auto profile = config::video.prores_profile;

  config::video.prores_mode = 0;
  config::video.prores_profile = "lt";
  config::apply_config({
    {"prores_mode", "9"},
    {"prores_profile", "bad_profile"},
  });

  EXPECT_EQ(config::video.prores_mode, 0);
  EXPECT_EQ(config::video.prores_profile, "lt");

  config::video.prores_mode = mode;
  config::video.prores_profile = profile;
}

TEST(ProResProtocolGateTest, KnownFormatsIncludeExperimentalProRes) {
  EXPECT_TRUE(video::is_known_video_format(video::SUNSHINE_FORMAT_H264));
  EXPECT_TRUE(video::is_known_video_format(video::SUNSHINE_FORMAT_HEVC));
  EXPECT_TRUE(video::is_known_video_format(video::SUNSHINE_FORMAT_AV1));
  EXPECT_TRUE(video::is_known_video_format(video::SUNSHINE_FORMAT_PRORES));
  EXPECT_FALSE(video::is_known_video_format(video::SUNSHINE_FORMAT_PRORES + 1));
}

TEST(ProResProtocolGateTest, ProResRequestsAreRejectedWhenDisabled) {
  EXPECT_TRUE(video::is_video_format_enabled_by_prores_gate(video::SUNSHINE_FORMAT_H264, 0));
  EXPECT_FALSE(video::is_video_format_enabled_by_prores_gate(video::SUNSHINE_FORMAT_PRORES, 0));
}

TEST(ProResProtocolGateTest, ProResRequestsAreAcceptedWhenEnabled) {
  EXPECT_TRUE(video::is_video_format_enabled_by_prores_gate(video::SUNSHINE_FORMAT_PRORES, 1));
  EXPECT_TRUE(video::is_video_format_enabled_by_prores_gate(video::SUNSHINE_FORMAT_PRORES, 2));
}

TEST_P(EncoderTest, ValidateEncoder) {
  // todo:: test something besides fixture setup
}

struct FramerateX100Test: testing::TestWithParam<std::tuple<std::int32_t, AVRational>> {};

TEST_P(FramerateX100Test, Run) {
  const auto &[x100, expected] = GetParam();
  auto res = video::framerateX100_to_rational(x100);
  ASSERT_EQ(0, av_cmp_q(res, expected)) << "expected "
                                        << expected.num << "/" << expected.den
                                        << ", got "
                                        << res.num << "/" << res.den;
}

INSTANTIATE_TEST_SUITE_P(
  FramerateX100Tests,
  FramerateX100Test,
  testing::Values(
    std::make_tuple(2397, AVRational {24000, 1001}),
    std::make_tuple(2398, AVRational {24000, 1001}),
    std::make_tuple(2500, AVRational {25, 1}),
    std::make_tuple(2997, AVRational {30000, 1001}),
    std::make_tuple(3000, AVRational {30, 1}),
    std::make_tuple(5994, AVRational {60000, 1001}),
    std::make_tuple(6000, AVRational {60, 1}),
    std::make_tuple(11988, AVRational {120000, 1001}),
    std::make_tuple(23976, AVRational {240000, 1001}),  // future NTSC 240hz?
    std::make_tuple(9498, AVRational {4749, 50})  // from my LG 27GN950
  )
);
