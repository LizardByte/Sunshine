/**
 * @file tests/unit/test_video.cpp
 * @brief Test src/video.*.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <tuple>
#include <utility>

// local includes
#include <src/config.h>
#include <src/video.h>

using namespace std::literals;

namespace {

  class bitrate_test_session_t: public video::encode_session_t {
  public:
    explicit bitrate_test_session_t(std::uint32_t initial_target_kbps = 0):
        last_target_kbps {initial_target_kbps} {
    }

    int convert(platf::img_t &) override {
      return 0;
    }

    void request_idr_frame() override {
      // Frame request side effects are outside this bitrate-only test double.
    }

    void request_normal_frame() override {
      // Frame request side effects are outside this bitrate-only test double.
    }

    void invalidate_ref_frames(int64_t, int64_t) override {
      // Reference-frame invalidation is outside this bitrate-only test double.
    }

    video::bitrate_reconfigure_result_t reconfigure_bitrate(std::uint32_t target_kbps) override {
      last_target_kbps = target_kbps;
      return {
        video::bitrate_reconfigure_status_e::applied,
        25'000,
        target_kbps,
        target_kbps,
      };
    }

    std::uint32_t last_target_kbps = 0;
  };

  class unsupported_bitrate_test_session_t: public video::encode_session_t {
  public:
    int convert(platf::img_t &) override {
      return 0;
    }

    void request_idr_frame() override {
      // Frame request side effects are outside this unsupported-backend test double.
    }

    void request_normal_frame() override {
      // Frame request side effects are outside this unsupported-backend test double.
    }

    void invalidate_ref_frames(int64_t, int64_t) override {
      // Reference-frame invalidation is outside this unsupported-backend test double.
    }
  };

}  // namespace

TEST(VideoBitrateReconfigureTest, StatusNamesAreStable) {
  EXPECT_EQ("applied", video::bitrate_reconfigure_status_name(video::bitrate_reconfigure_status_e::applied));
  EXPECT_EQ("unchanged", video::bitrate_reconfigure_status_name(video::bitrate_reconfigure_status_e::unchanged));
  EXPECT_EQ("invalid", video::bitrate_reconfigure_status_name(video::bitrate_reconfigure_status_e::invalid));
  EXPECT_EQ("unsupported", video::bitrate_reconfigure_status_name(video::bitrate_reconfigure_status_e::unsupported));
  EXPECT_EQ("failed", video::bitrate_reconfigure_status_name(video::bitrate_reconfigure_status_e::failed));
  EXPECT_EQ("unknown", video::bitrate_reconfigure_status_name(static_cast<video::bitrate_reconfigure_status_e>(99)));
}

TEST(VideoBitrateReconfigureTest, LatestPendingRequestWins) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto bitrate_events = mail->event<video::bitrate_reconfigure_request_t>("test-video-bitrate");
  bitrate_test_session_t session;
  video::config_t config {};
  config.bitrate = 25'000;

  EXPECT_FALSE(video::apply_pending_bitrate_reconfiguration(bitrate_events, session, config).has_value());
  bitrate_events->raise(video::bitrate_reconfigure_request_t {25'000});
  bitrate_events->raise(video::bitrate_reconfigure_request_t {40'000});

  const auto result = video::apply_pending_bitrate_reconfiguration(bitrate_events, session, config);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(video::bitrate_reconfigure_status_e::applied, result->status);
  EXPECT_EQ(40'000U, session.last_target_kbps);
  EXPECT_EQ(40'000, config.bitrate);
  EXPECT_FALSE(video::apply_pending_bitrate_reconfiguration(bitrate_events, session, config).has_value());
}

TEST(VideoBitrateReconfigureTest, SuccessfulRequestPersistsAcrossEncoderReinitialization) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto bitrate_events = mail->event<video::bitrate_reconfigure_request_t>("test-video-bitrate-reinit");
  bitrate_test_session_t initial_session;
  video::config_t config {};
  config.bitrate = 25'000;

  bitrate_events->raise(video::bitrate_reconfigure_request_t {40'000});
  const auto result = video::apply_pending_bitrate_reconfiguration(bitrate_events, initial_session, config);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(video::bitrate_reconfigure_status_e::applied, result->status);

  bitrate_test_session_t reinitialized_session {static_cast<std::uint32_t>(config.bitrate)};
  EXPECT_EQ(40'000U, reinitialized_session.last_target_kbps);
}

TEST(VideoBitrateReconfigureTest, UnsupportedBackendKeepsFixedBehavior) {
  unsupported_bitrate_test_session_t session;
  const auto result = session.reconfigure_bitrate(40'000);

  EXPECT_EQ(video::bitrate_reconfigure_status_e::unsupported, result.status);
  EXPECT_EQ(0U, result.old_target_kbps);
  EXPECT_EQ(40'000U, result.requested_target_kbps);
  EXPECT_EQ(0U, result.effective_target_kbps);
}

struct EncoderTest: PlatformTestSuite, testing::WithParamInterface<video::encoder_t *> {
  void SetUp() override {
    BaseTest::SetUp();
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

TEST_P(EncoderTest, ValidateEncoder) {
  // todo:: test something besides fixture setup
}

/**
 * @brief Parameterized coverage for effective H.264 profile selection.
 */
struct H264ProfileTest: testing::TestWithParam<std::tuple<std::string_view, video::amf::coder_e, int, int>> {};

TEST_P(H264ProfileTest, SelectProfile) {
  const auto &[encoder_name, coder, chroma_sampling_type, expected_profile] = GetParam();
  video::config_t config {};
  config.chromaSamplingType = chroma_sampling_type;

  EXPECT_EQ(expected_profile, video::select_h264_profile(encoder_name, config, std::to_underlying(coder)));
}

INSTANTIATE_TEST_SUITE_P(
  H264ProfileTests,
  H264ProfileTest,
  testing::Values(
    std::make_tuple("h264_amf"sv, video::amf::coder_e::auto_, 0, AV_PROFILE_H264_HIGH),
    std::make_tuple("h264_amf"sv, video::amf::coder_e::cabac, 0, AV_PROFILE_H264_HIGH),
    std::make_tuple("h264_amf"sv, video::amf::coder_e::cavlc, 0, AV_PROFILE_H264_CONSTRAINED_BASELINE),
    std::make_tuple("h264_amf"sv, video::amf::coder_e::cavlc, 1, AV_PROFILE_H264_HIGH_444_PREDICTIVE),
    std::make_tuple("h264_nvenc"sv, video::amf::coder_e::cavlc, 0, AV_PROFILE_H264_HIGH)
  )
);

#ifdef _WIN32
TEST(AmfH264OptionsTest, CoderUsesConfiguredValue) {
  const auto coder_option = std::ranges::find(video::amdvce.h264.common_options, "coder"sv, &video::encoder_t::option_t::name);

  ASSERT_NE(video::amdvce.h264.common_options.end(), coder_option);
  ASSERT_TRUE(std::holds_alternative<int *>(coder_option->value));
  EXPECT_EQ(&config::video.amd.amd_coder, std::get<int *>(coder_option->value));
}
#endif

struct FramerateX100Test: BaseTest, testing::WithParamInterface<std::tuple<std::int32_t, AVRational>> {};

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

struct FramerateToRationalTest: testing::TestWithParam<std::tuple<int, int, AVRational>> {};

TEST_P(FramerateToRationalTest, Run) {
  const auto &[framerate, framerateX100, expected] = GetParam();
  video::config_t config {};
  config.framerate = framerate;
  config.framerateX100 = framerateX100;
  auto res = video::framerate_to_rational(config);
  ASSERT_EQ(0, av_cmp_q(res, expected)) << "expected "
                                        << expected.num << "/" << expected.den
                                        << ", got "
                                        << res.num << "/" << res.den;
}

INSTANTIATE_TEST_SUITE_P(
  FramerateToRationalTests,
  FramerateToRationalTest,
  testing::Values(
    std::make_tuple(60, 0, AVRational {60, 1}),  // no X100 value, fall back to integer framerate
    std::make_tuple(60, 5994, AVRational {60000, 1001}),
    std::make_tuple(120, 11988, AVRational {120000, 1001}),
    std::make_tuple(24, 2398, AVRational {24000, 1001})
  )
);

struct CaptureFrameIntervalTest: testing::TestWithParam<std::tuple<int, int, std::chrono::nanoseconds>> {};

TEST_P(CaptureFrameIntervalTest, Run) {
  const auto &[framerate, framerateX100, expected] = GetParam();
  video::config_t config {};
  config.framerate = framerate;
  config.framerateX100 = framerateX100;
  ASSERT_EQ(expected, video::capture_frame_interval(config));
}

INSTANTIATE_TEST_SUITE_P(
  CaptureFrameIntervalTests,
  CaptureFrameIntervalTest,
  testing::Values(
    std::make_tuple(60, 0, std::chrono::nanoseconds {16666666}),
    std::make_tuple(60, 5994, std::chrono::nanoseconds {16683333}),  // 1e9 * 1001 / 60000
    std::make_tuple(120, 11988, std::chrono::nanoseconds {8341666})  // 1e9 * 1001 / 120000
  )
);
