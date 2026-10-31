/**
 * @file tests/unit/test_video.cpp
 * @brief Test src/video.*.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// ffmpeg includes
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// local includes
#include <src/config.h>
#include <src/video.h>

using namespace std::literals;

namespace video {
  input::touch_port_t make_port(platf::display_t *display, const config_t &config, const std::optional<platf::source_crop_t> &source_crop);
}  // namespace video

/**
 * @brief Packet replacement rules retain the original non-owning byte views.
 */
TEST(VideoPacketReplacementTest, CopiesViewsWithoutCopyingTheirBackingBytes) {
  std::string original = "__old__";
  std::string replacement = "__new__";
  const video::packet_raw_t::replace_t rule {std::string_view(original).substr(2, 3), std::string_view(replacement).substr(2, 3)};
  EXPECT_EQ(rule.old, "old");
  EXPECT_EQ(rule._new, "new");
  EXPECT_EQ(rule.old.data(), original.data() + 2);
  EXPECT_EQ(rule._new.data(), replacement.data() + 2);
  original[2] = 'O';
  replacement[2] = 'N';
  EXPECT_EQ(rule.old, "Old");
  EXPECT_EQ(rule._new, "New");
}

TEST(VideoFormatMappingTest, MapsSupportedHardwareDevicesAndRejectsUnsupportedOnes) {
  using enum platf::mem_type_e;
  const std::array cases {
    std::pair {AV_HWDEVICE_TYPE_D3D11VA, dxgi},
    std::pair {AV_HWDEVICE_TYPE_VAAPI, vaapi},
    std::pair {AV_HWDEVICE_TYPE_CUDA, cuda},
    std::pair {AV_HWDEVICE_TYPE_NONE, system},
    std::pair {AV_HWDEVICE_TYPE_VIDEOTOOLBOX, videotoolbox},
#ifdef SUNSHINE_BUILD_VULKAN
    std::pair {AV_HWDEVICE_TYPE_VULKAN, vulkan},
#else
    std::pair {AV_HWDEVICE_TYPE_VULKAN, unknown},
#endif
    std::pair {AV_HWDEVICE_TYPE_DXVA2, unknown},
  };

  for (const auto &[format, expected] : cases) {
    EXPECT_EQ(video::map_base_dev_type(format), expected) << format;
  }
}

TEST(VideoFormatMappingTest, MapsSupportedPixelFormatsAndRejectsUnsupportedOnes) {
  using enum platf::pix_fmt_e;
  const std::array cases {
    std::pair {AV_PIX_FMT_VUYX, ayuv},
    std::pair {AV_PIX_FMT_XV30, y410},
    std::pair {AV_PIX_FMT_YUV420P10, yuv420p10},
    std::pair {AV_PIX_FMT_YUV420P, yuv420p},
    std::pair {AV_PIX_FMT_NV12, nv12},
    std::pair {AV_PIX_FMT_P010, p010},
    std::pair {AV_PIX_FMT_YUV444P, yuv444p},
    std::pair {AV_PIX_FMT_YUV444P16, yuv444p16},
    std::pair {AV_PIX_FMT_NONE, unknown},
    std::pair {AV_PIX_FMT_RGB24, unknown},
  };

  for (const auto &[format, expected] : cases) {
    EXPECT_EQ(video::map_pix_fmt(format), expected) << format;
  }
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

/**
 * @brief Available encoder backends for this platform.
 */
const auto encoder_variants = std::to_array<video::encoder_t *>({
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
});

INSTANTIATE_TEST_SUITE_P(
  EncoderVariants,
  EncoderTest,
  testing::ValuesIn(encoder_variants),
  [](const auto &info) {
    return std::string(info.param->name);
  }
);

#if !defined(_WIN32) && !defined(__APPLE__)
TEST(NvencAvcodecOptionsTest, UsesVersionStablePresetAndSpatialAqNames) {
  const std::array codecs {
    &video::nvenc.av1,
    &video::nvenc.hevc,
    &video::nvenc.h264,
  };

  for (const auto *codec : codecs) {
    const auto preset = std::ranges::find(codec->common_options, "preset"sv, &video::encoder_t::option_t::name);
    ASSERT_NE(codec->common_options.end(), preset);
    ASSERT_TRUE(std::holds_alternative<std::string *>(preset->value));
    EXPECT_EQ(&config::video.nv_legacy.preset, std::get<std::string *>(preset->value));

    const auto spatial_aq = std::ranges::find(codec->common_options, "spatial-aq"sv, &video::encoder_t::option_t::name);
    ASSERT_NE(codec->common_options.end(), spatial_aq);
    ASSERT_TRUE(std::holds_alternative<int *>(spatial_aq->value));
    EXPECT_EQ(&config::video.nv_legacy.spatial_aq, std::get<int *>(spatial_aq->value));

    EXPECT_EQ(
      codec->common_options.end(),
      std::ranges::find(codec->common_options, "aq"sv, &video::encoder_t::option_t::name)
    );
  }
}
#endif

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

/**
 * @brief Parameterized coverage for resolving requested dynamic range against encoder capabilities.
 */
struct DynamicRangeTest: testing::TestWithParam<std::tuple<int, int, int, bool, bool, int>> {};

TEST_P(DynamicRangeTest, Resolve) {
  const auto &[video_format, chroma_sampling_type, requested_dynamic_range, supports_hdr, supports_hdr_yuv444, expected_dynamic_range] = GetParam();

  video::encoder_t encoder {
    "test"sv,
    {},
    {},
    {},
    {},
    0,
  };
  encoder.h264.name = "h264_test";
  encoder.hevc.name = "hevc_test";
  encoder.av1.name = "av1_test";

  video::config_t config {};
  config.videoFormat = video_format;
  config.dynamicRange = requested_dynamic_range;
  config.chromaSamplingType = chroma_sampling_type;

  auto *codec = &encoder.h264;
  if (video_format == 1) {
    codec = &encoder.hevc;
  } else if (video_format == 2) {
    codec = &encoder.av1;
  }
  (*codec)[video::encoder_t::DYNAMIC_RANGE] = supports_hdr;
  (*codec)[video::encoder_t::DYNAMIC_RANGE_YUV444] = supports_hdr_yuv444;

  const auto effective_config = video::resolve_dynamic_range(encoder, config);

  EXPECT_EQ(expected_dynamic_range, effective_config.dynamicRange);
  EXPECT_EQ(requested_dynamic_range, config.dynamicRange);
  EXPECT_EQ(video_format, effective_config.videoFormat);
  EXPECT_EQ(chroma_sampling_type, effective_config.chromaSamplingType);
}

INSTANTIATE_TEST_SUITE_P(
  DynamicRangeTests,
  DynamicRangeTest,
  testing::Values(
    std::make_tuple(0, 0, 1, false, true, 0),
    std::make_tuple(1, 0, 1, false, true, 0),
    std::make_tuple(1, 0, 1, true, false, 1),
    std::make_tuple(2, 1, 1, true, false, 0),
    std::make_tuple(2, 1, 1, false, true, 1),
    std::make_tuple(1, 0, 0, false, false, 0)
  )
);

/**
 * @brief Reset the published probe results before and after each test.
 */
struct EncoderProbeResultsTest: testing::Test {
  /**
   * @brief Start from an empty published state.
   */
  void SetUp() override {
    video::reset_encoder_probe_results();
  }

  /**
   * @brief Leave no published state behind for other tests.
   */
  void TearDown() override {
    video::reset_encoder_probe_results();
  }
};

TEST_F(EncoderProbeResultsTest, FailedCodecProbeDoesNotPublishCapabilities) {
  video::encoder_t encoder {"test"sv, {}, {}, {}, {}, 0};
  for (auto *codec : {&encoder.h264, &encoder.hevc, &encoder.av1}) {
    (*codec)[video::encoder_t::PASSED] = false;
    (*codec)[video::encoder_t::YUV444] = true;
    (*codec)[video::encoder_t::DYNAMIC_RANGE] = true;
    (*codec)[video::encoder_t::DYNAMIC_RANGE_YUV444] = true;
  }

  video::record_encoder_probe_results(encoder);

  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(video::last_encoder_probe_supported_yuv444_for_codec[i]) << i;
    EXPECT_FALSE(video::last_encoder_probe_supported_dynamic_range_for_codec[i]) << i;
    EXPECT_FALSE(video::last_encoder_probe_supported_dynamic_range_yuv444_for_codec[i]) << i;
  }
}

TEST_F(EncoderProbeResultsTest, SubsequentProbeReplacesPriorResults) {
  constexpr uint32_t ref_frames_invalidation = 1 << 8;  // Mirrors REF_FRAMES_INVALIDATION in video.cpp.
  video::encoder_t first {"first"sv, {}, {}, {}, {}, ref_frames_invalidation};
  for (auto *codec : {&first.h264, &first.hevc, &first.av1}) {
    (*codec)[video::encoder_t::PASSED] = true;
    (*codec)[video::encoder_t::YUV444] = true;
    (*codec)[video::encoder_t::DYNAMIC_RANGE] = true;
    (*codec)[video::encoder_t::DYNAMIC_RANGE_YUV444] = true;
  }

  // The second encoder only supports plain 8-bit 4:2:0 H.264.
  video::encoder_t second {"second"sv, {}, {}, {}, {}, 0};
  second.h264[video::encoder_t::PASSED] = true;

  video::record_encoder_probe_results(first);
  ASSERT_TRUE(video::last_encoder_probe_supported_ref_frames_invalidation);
  ASSERT_TRUE(video::last_encoder_probe_supported_yuv444_for_codec[1]);
  ASSERT_TRUE(video::last_encoder_probe_supported_dynamic_range_for_codec[2]);
  ASSERT_TRUE(video::last_encoder_probe_supported_dynamic_range_yuv444_for_codec[2]);

  video::record_encoder_probe_results(second);

  EXPECT_FALSE(video::last_encoder_probe_supported_ref_frames_invalidation);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(video::last_encoder_probe_supported_yuv444_for_codec[i]) << i;
    EXPECT_FALSE(video::last_encoder_probe_supported_dynamic_range_for_codec[i]) << i;
    EXPECT_FALSE(video::last_encoder_probe_supported_dynamic_range_yuv444_for_codec[i]) << i;
  }
}

#ifdef _WIN32
TEST(AmfH264OptionsTest, CoderUsesConfiguredValue) {
  const auto coder_option = std::ranges::find(video::amdvce.h264.common_options, "coder"sv, &video::encoder_t::option_t::name);

  ASSERT_NE(video::amdvce.h264.common_options.end(), coder_option);
  ASSERT_TRUE(std::holds_alternative<int *>(coder_option->value));
  EXPECT_EQ(&config::video.amd.amd_coder, std::get<int *>(coder_option->value));
}

/**
 * @brief Parameterized coverage for the AMF maximum access-unit-size option mappings.
 */
struct AmfMaxAuSizeOptionsTest: testing::TestWithParam<std::tuple<const video::encoder_t::codec_t *, bool>> {};

TEST_P(AmfMaxAuSizeOptionsTest, UsesConfiguredValueForSupportedCodecsOnly) {
  const auto &[codec, supported] = GetParam();
  const auto option = std::ranges::find(codec->common_options, "max_au_size"sv, &video::encoder_t::option_t::name);

  if (!supported) {
    EXPECT_EQ(codec->common_options.end(), option);
    return;
  }

  ASSERT_NE(codec->common_options.end(), option);
  ASSERT_TRUE(std::holds_alternative<std::optional<int> *>(option->value));
  EXPECT_EQ(&config::video.amd.amd_max_au_size, std::get<std::optional<int> *>(option->value));
}

INSTANTIATE_TEST_SUITE_P(
  AmfCodecOptions,
  AmfMaxAuSizeOptionsTest,
  testing::Values(
    std::make_tuple(&video::amdvce.h264, true),
    std::make_tuple(&video::amdvce.hevc, true),
    std::make_tuple(&video::amdvce.av1, false)
  )
);
#endif

using AmfMaxAuSizeConfigParam = std::tuple<std::string_view, std::optional<int>>;

/**
 * @brief Parameterized coverage for parsing and validating the AMF maximum access-unit size.
 */
struct AmfMaxAuSizeConfigTest: BaseTest, testing::WithParamInterface<AmfMaxAuSizeConfigParam> {
  void SetUp() override {
    BaseTest::SetUp();
    config::video.amd.amd_max_au_size.reset();
    config::stream.file_apps = SUNSHINE_SOURCE_DIR "/tests/unit/test_video.cpp";
  }

  void TearDown() override {
    config::video = original_video;
    config::audio = original_audio;
    config::stream = original_stream;
    config::nvhttp = original_nvhttp;
    config::input = original_input;
    config::sunshine = original_sunshine;
    config::modified_config_settings = original_modified_config_settings;
    BaseTest::TearDown();
  }

  config::video_t original_video {config::video};  ///< Video configuration restored after each parameterized test.
  config::audio_t original_audio {config::audio};  ///< Audio configuration restored after each parameterized test.
  config::stream_t original_stream {config::stream};  ///< Stream configuration restored after each parameterized test.
  config::nvhttp_t original_nvhttp {config::nvhttp};  ///< HTTP configuration restored after each parameterized test.
  config::input_t original_input {config::input};  ///< Input configuration restored after each parameterized test.
  config::sunshine_t original_sunshine {config::sunshine};  ///< Core configuration restored after each parameterized test.
  decltype(config::modified_config_settings) original_modified_config_settings {config::modified_config_settings};  ///< Modified settings restored after each parameterized test.
};

TEST_P(AmfMaxAuSizeConfigTest, AcceptsOnlyFfmpegSupportedRange) {
  const auto &[setting, expected] = GetParam();
  config::apply_config_for_test(setting);

  EXPECT_EQ(expected, config::video.amd.amd_max_au_size);
}

INSTANTIATE_TEST_SUITE_P(
  AmfMaxAuSizeValues,
  AmfMaxAuSizeConfigTest,
  testing::Values(
    AmfMaxAuSizeConfigParam {""sv, std::nullopt},
    AmfMaxAuSizeConfigParam {"amd_max_au_size = -2\n"sv, std::nullopt},
    AmfMaxAuSizeConfigParam {"amd_max_au_size = -1\n"sv, -1},
    AmfMaxAuSizeConfigParam {"amd_max_au_size = 0\n"sv, 0},
    AmfMaxAuSizeConfigParam {"amd_max_au_size = 800000\n"sv, 800000},
    AmfMaxAuSizeConfigParam {"amd_max_au_size = 2147483647\n"sv, std::numeric_limits<int>::max()}
  )
);

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

/**
 * @brief Software encoder converts BGR0 and NV12 frames, including padded strides and
 *        backends that don't report the pixel pitch.
 */
/**
 * @brief The host setting decides, and only "client" lets the client's request through.
 */
TEST(SourceAspectRatioTest, HostSettingOverridesTheClientRequest) {
  EXPECT_EQ(video::select_source_aspect_ratio("client", "", 1920, 1200), std::pair(0, 0));
  EXPECT_EQ(video::select_source_aspect_ratio("client", "16:9", 1920, 1200), std::pair(16, 9));
  EXPECT_EQ(video::select_source_aspect_ratio("client", "nonsense", 1920, 1200), std::pair(0, 0));
  EXPECT_EQ(video::select_source_aspect_ratio("display", "16:9", 1920, 1200), std::pair(0, 0));
  EXPECT_EQ(video::select_source_aspect_ratio("stream", "16:9", 1920, 1200), std::pair(1920, 1200));
  EXPECT_EQ(video::select_source_aspect_ratio("21:9", "16:9", 1920, 1200), std::pair(21, 9));
}

namespace {
  /**
   * @brief Create a stream configuration that requests a source aspect ratio.
   *
   * @param ratio_width Width of the requested ratio.
   * @param ratio_height Height of the requested ratio.
   * @return Stream configuration with the ratio set.
   */
  video::config_t crop_config(int ratio_width, int ratio_height) {
    video::config_t config {};
    config.sourceAspectRatioWidth = ratio_width;
    config.sourceAspectRatioHeight = ratio_height;
    return config;
  }

  /**
   * @brief Display with a fixed size that never captures anything.
   */
  struct fixed_size_display_t: platf::display_t {
    /**
     * @brief Create a display on the right of a primary display.
     *
     * @param display_width Width of the display in pixels.
     * @param display_height Height of the display in pixels.
     */
    fixed_size_display_t(int display_width, int display_height) {
      width = display_width;
      height = display_height;
      offset_x = 1920;
      offset_y = 40;
      env_width = 1920 + display_width;
      env_height = display_height;
    }

    /**
     * @brief Report a capture error, as this display has no images.
     *
     * @return Always a capture error.
     */
    platf::capture_e capture(const push_captured_image_cb_t &, const pull_free_image_cb_t &, bool *) override {
      return platf::capture_e::capture_error;
    }

    /**
     * @brief Refuse to allocate images.
     *
     * @return Always null.
     */
    std::shared_ptr<platf::img_t> alloc_img() override {
      return nullptr;
    }

    /**
     * @brief Refuse to fill images.
     *
     * @return Always a failure status.
     */
    int dummy_img(platf::img_t *) override {
      return -1;
    }
  };
}  // namespace

/**
 * @brief No crop is applied without a request, or when the source already fits.
 */
TEST(SourceCropTest, LeavesTheWholeImageWhenThereIsNothingToCrop) {
  EXPECT_FALSE(video::compute_source_crop(7680, 2160, crop_config(0, 0)).has_value());
  EXPECT_FALSE(video::compute_source_crop(3840, 2160, crop_config(16, 9)).has_value());
  EXPECT_FALSE(video::compute_source_crop(3840, 2160, crop_config(1920, 1080)).has_value());
  EXPECT_FALSE(video::compute_source_crop(0, 0, crop_config(16, 9)).has_value());
}

/**
 * @brief A display wider than the requested ratio loses its left and right edges.
 */
TEST(SourceCropTest, CentersHorizontallyOnWiderDisplays) {
  const auto crop = video::compute_source_crop(7680, 2160, crop_config(16, 10));
  ASSERT_TRUE(crop.has_value());
  EXPECT_EQ(crop->x, 2112);
  EXPECT_EQ(crop->y, 0);
  EXPECT_EQ(crop->width, 3456);
  EXPECT_EQ(crop->height, 2160);
}

/**
 * @brief A display taller than the requested ratio loses its top and bottom edges.
 */
TEST(SourceCropTest, CentersVerticallyOnTallerDisplays) {
  const auto crop = video::compute_source_crop(2560, 1600, crop_config(21, 9));
  ASSERT_TRUE(crop.has_value());
  EXPECT_EQ(crop->x, 0);
  EXPECT_EQ(crop->y, 252);
  EXPECT_EQ(crop->width, 2560);
  EXPECT_EQ(crop->height, 1096);
}

/**
 * @brief Regions stay even and inside the image whatever the ratio.
 */
TEST(SourceCropTest, KeepsCoordinatesEvenAndInsideTheImage) {
  for (const auto &[ratio_width, ratio_height] : {std::pair(1, 1), std::pair(3, 2), std::pair(1000, 999), std::pair(1, 1000000), std::pair(1000000, 1), std::pair(2147483647, 2147483646)}) {
    for (const auto &[width, height] : {std::pair(1366, 768), std::pair(1367, 769), std::pair(5120, 1440)}) {
      const auto crop = video::compute_source_crop(width, height, crop_config(ratio_width, ratio_height));
      if (!crop) {
        continue;
      }
      EXPECT_EQ(crop->x % 2, 0);
      EXPECT_EQ(crop->y % 2, 0);
      EXPECT_EQ(crop->width % 2, 0);
      EXPECT_EQ(crop->height % 2, 0);
      EXPECT_GE(crop->width, 2);
      EXPECT_GE(crop->height, 2);
      EXPECT_LE(crop->x + crop->width, width);
      EXPECT_LE(crop->y + crop->height, height);
    }
  }
}

/**
 * @brief Input stays mapped to the whole display when nothing is cropped.
 */
TEST(SourceCropInputTest, MapsTheWholeDisplayWithoutACrop) {
  fixed_size_display_t display {7680, 2160};
  video::config_t config {};
  config.width = 1920;
  config.height = 1080;

  const auto port = video::make_port(&display, config, std::nullopt);

  EXPECT_EQ(port.offset_x, 1920);
  EXPECT_EQ(port.offset_y, 40);
  EXPECT_FLOAT_EQ(port.client_offsetX, 0.0f);
  EXPECT_FLOAT_EQ(port.client_offsetY, 270.0f);
  EXPECT_FLOAT_EQ(port.scalar_inv, 4.0f);
}

/**
 * @brief Client coordinates cover only the crop, which starts away from the display's edge.
 */
TEST(SourceCropInputTest, MapsTheClientToTheCroppedRegion) {
  fixed_size_display_t display {7680, 2160};
  video::config_t config {};
  config.width = 1920;
  config.height = 1200;

  const auto port = video::make_port(&display, config, platf::source_crop_t {2112, 0, 3456, 2160});

  EXPECT_EQ(port.offset_x, 1920 + 2112);
  EXPECT_EQ(port.offset_y, 40);
  EXPECT_EQ(port.width, 1920);
  EXPECT_EQ(port.height, 1200);
  EXPECT_EQ(port.env_width, 1920 + 7680);
  EXPECT_NEAR(port.client_offsetX, 0.0f, 0.001f);
  EXPECT_NEAR(port.client_offsetY, 0.0f, 0.001f);
  EXPECT_FLOAT_EQ(port.scalar_inv, 1.8f);
}

/**
 * @brief Black bars around a crop that doesn't match the stream are left out of the mapping.
 */
TEST(SourceCropInputTest, AccountsForBarsAroundTheCrop) {
  fixed_size_display_t display {7680, 2160};
  video::config_t config {};
  config.width = 1920;
  config.height = 1080;

  const auto port = video::make_port(&display, config, platf::source_crop_t {2112, 0, 3456, 2160});

  EXPECT_FLOAT_EQ(port.client_offsetX, 96.0f);
  EXPECT_FLOAT_EQ(port.client_offsetY, 0.0f);
  EXPECT_FLOAT_EQ(port.scalar_inv, 2.0f);
}

/**
 * @brief Only software conversion crops unless a hardware device says otherwise.
 */
TEST(SourceCropDeviceTest, SupportDefaultsToSoftwareDevices) {
  platf::avcodec_encode_device_t device;
  EXPECT_TRUE(device.supports_source_crop());

  device.hardware = true;
  EXPECT_FALSE(device.supports_source_crop());
}

TEST(SoftwareEncoderConversion, Bgr0AndNv12) {
  constexpr int w = 320;
  constexpr int h = 240;

  AVFrame *frame = av_frame_alloc();
  ASSERT_NE(frame, nullptr);
  frame->width = w;
  frame->height = h;
  frame->format = AV_PIX_FMT_YUV420P;

  video::avcodec_software_encode_device_t device;
  ASSERT_EQ(device.init(w, h, frame, AV_PIX_FMT_YUV420P, false), 0);
  // set_frame() takes ownership of the frame; the device frees it on destruction.
  ASSERT_EQ(device.set_frame(frame, nullptr), 0);

  // BGR0 frame (4 bytes per pixel) -- the classic KMS/DMABUF capture layout.
  std::vector<uint8_t> bgr0_buffer(static_cast<size_t>(w) * h * 4);
  platf::img_t bgr0_img {};
  bgr0_img.data = bgr0_buffer.data();
  bgr0_img.width = w;
  bgr0_img.height = h;
  bgr0_img.row_pitch = w * 4;
  bgr0_img.pixel_pitch = 4;
  EXPECT_EQ(device.convert(bgr0_img), 0);

  // NV12 frame (1 byte per pixel row pitch, Y plane + interleaved UV) -- the
  // layout delivered by PipeWire-based captures (KWin screencast / portal).
  std::vector<uint8_t> nv12_buffer(static_cast<size_t>(w) * h + static_cast<size_t>(w) * h / 2);
  platf::img_t nv12_img {};
  nv12_img.data = nv12_buffer.data();
  nv12_img.width = w;
  nv12_img.height = h;
  nv12_img.row_pitch = w;
  nv12_img.pixel_pitch = 1;
  EXPECT_EQ(device.convert(nv12_img), 0);

  // Padded-stride NV12 (alignment padding) -- the case the old row_pitch ==
  // width heuristic misdetected as BGR0, causing out-of-bounds reads.
  constexpr int padded_stride = w + 32;
  std::vector<uint8_t> padded_nv12_buffer(static_cast<size_t>(padded_stride) * h + static_cast<size_t>(padded_stride) * h / 2);
  platf::img_t padded_nv12_img {};
  padded_nv12_img.data = padded_nv12_buffer.data();
  padded_nv12_img.width = w;
  padded_nv12_img.height = h;
  padded_nv12_img.row_pitch = padded_stride;
  padded_nv12_img.pixel_pitch = 1;
  EXPECT_EQ(device.convert(padded_nv12_img), 0);

  // Capture backends that don't report pixel_pitch fall back to deriving it
  // from the row pitch (1 byte per pixel = NV12, 4 = BGR0).
  platf::img_t fallback_bgr0_img {};
  fallback_bgr0_img.data = bgr0_buffer.data();
  fallback_bgr0_img.width = w;
  fallback_bgr0_img.height = h;
  fallback_bgr0_img.row_pitch = w * 4;
  EXPECT_EQ(device.convert(fallback_bgr0_img), 0);

  platf::img_t fallback_nv12_img {};
  fallback_nv12_img.data = nv12_buffer.data();
  fallback_nv12_img.width = w;
  fallback_nv12_img.height = h;
  fallback_nv12_img.row_pitch = w;
  EXPECT_EQ(device.convert(fallback_nv12_img), 0);
}

/**
 * @brief The software path encodes only the source crop of BGR0 and NV12 captures.
 */
TEST(SoftwareEncoderConversion, EncodesOnlyTheSourceCrop) {
  constexpr int capture_w = 640;
  constexpr int capture_h = 480;
  constexpr platf::source_crop_t crop {160, 120, 320, 240};
  constexpr std::uint8_t bright = 0xFF;

  AVFrame *frame = av_frame_alloc();
  ASSERT_NE(frame, nullptr);
  frame->width = crop.width;
  frame->height = crop.height;
  frame->format = AV_PIX_FMT_YUV420P;

  video::avcodec_software_encode_device_t device;
  device.source_crop = crop;
  ASSERT_EQ(device.init(crop.width, crop.height, frame, AV_PIX_FMT_YUV420P, false), 0);
  ASSERT_EQ(device.set_frame(frame, nullptr), 0);

  const auto expect_bright_luma = [&]() {
    for (int y = 0; y < crop.height; ++y) {
      for (int x = 0; x < crop.width; ++x) {
        ASSERT_GT(frame->data[0][y * frame->linesize[0] + x], 200) << "pixel " << x << ',' << y;
      }
    }
  };

  // Everything outside of the crop stays black, so any of it reaching the frame would darken it.
  std::vector<std::uint8_t> bgr0_buffer(static_cast<std::size_t>(capture_w) * capture_h * 4);
  for (int y = crop.y; y < crop.y + crop.height; ++y) {
    std::fill_n(bgr0_buffer.begin() + (y * capture_w + crop.x) * 4, crop.width * 4, bright);
  }
  platf::img_t bgr0_img {};
  bgr0_img.data = bgr0_buffer.data();
  bgr0_img.width = capture_w;
  bgr0_img.height = capture_h;
  bgr0_img.row_pitch = capture_w * 4;
  bgr0_img.pixel_pitch = 4;
  ASSERT_EQ(device.convert(bgr0_img), 0);
  expect_bright_luma();

  std::vector<std::uint8_t> nv12_buffer(static_cast<std::size_t>(capture_w) * capture_h * 3 / 2, 0x80);
  std::fill_n(nv12_buffer.begin(), capture_w * capture_h, 0);
  for (int y = crop.y; y < crop.y + crop.height; ++y) {
    std::fill_n(nv12_buffer.begin() + y * capture_w + crop.x, crop.width, bright);
  }
  platf::img_t nv12_img {};
  nv12_img.data = nv12_buffer.data();
  nv12_img.width = capture_w;
  nv12_img.height = capture_h;
  nv12_img.row_pitch = capture_w;
  nv12_img.pixel_pitch = 1;
  std::fill_n(frame->data[0], static_cast<std::size_t>(frame->linesize[0]) * crop.height, 0);
  ASSERT_EQ(device.convert(nv12_img), 0);
  expect_bright_luma();
}
