/**
 * @file tests/unit/test_video.cpp
 * @brief Test src/video.*.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// ffmpeg includes
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

// local includes
#include <src/config.h>
#include <src/video.h>

using namespace std::literals;

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

namespace {
  /**
   * @brief Release an FFmpeg frame.
   *
   * @param frame Frame to release.
   */
  void free_frame(AVFrame *frame) {
    av_frame_free(&frame);
  }

  /// Owning FFmpeg frame pointer that still releases the frame when a gtest assertion aborts the test.
  using avframe_ptr = std::unique_ptr<AVFrame, decltype(&free_frame)>;

  /**
   * @brief Initialize a software conversion device with a padded output frame
   *        and a defined colorimetry.
   *
   * @param device Device to initialize.
   * @param in_w Input frame width in pixels.
   * @param in_h Input frame height in pixels.
   * @param out_w Output frame width in pixels.
   * @param out_h Output frame height in pixels.
   * @param format Output pixel format.
   */
  void setup_conversion(video::avcodec_software_encode_device_t &device, int in_w, int in_h, int out_w, int out_h, AVPixelFormat format) {
    avframe_ptr frame(av_frame_alloc(), &free_frame);
    ASSERT_NE(frame, nullptr);
    frame->width = out_w;
    frame->height = out_h;
    frame->format = format;

    ASSERT_EQ(device.init(in_w, in_h, frame.get(), format, false), 0);
    ASSERT_EQ(device.set_frame(frame.get(), nullptr), 0);
    frame.release();
    ASSERT_NE(device.frame, nullptr);
    ASSERT_NE(device.frame->data[0], nullptr);
    ASSERT_NE(device.frame->data[2], nullptr);

    // Rec. 601 matches the swscale defaults used by the initial context, and
    // applying it here keeps the first conversion on the same matrix and range
    // as the conversions that re-apply the colorspace after a format switch.
    device.colorspace = {video::colorspace_e::rec601, false, static_cast<unsigned>(av_pix_fmt_desc_get(format)->comp[0].depth)};
    device.apply_colorspace();
  }

  /**
   * @brief Build a saturated red BGR0 buffer.
   *
   * @param width Buffer width in pixels.
   * @param height Buffer height in pixels.
   * @return Tightly packed BGR0 buffer.
   */
  std::vector<std::uint8_t> make_red_bgr0(int width, int height) {
    std::vector<std::uint8_t> bgr0(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
      bgr0[i * 4 + 2] = 0xFF;
    }
    return bgr0;
  }

  /**
   * @brief Point a capture image at a BGR0 buffer.
   *
   * @param img Image to initialize.
   * @param buffer Buffer backing the image.
   * @param width Image width in pixels.
   * @param height Image height in pixels.
   */
  void init_bgr0_img(platf::img_t &img, std::vector<std::uint8_t> &buffer, int width, int height) {
    img.data = buffer.data();
    img.width = width;
    img.height = height;
    img.row_pitch = width * 4;
    img.pixel_pitch = 4;
    img.pixel_format = platf::pix_fmt_e::bgr0;
  }

  /**
   * @brief Initialize a padded device and convert a saturated red frame.
   *
   * @param device Device to initialize.
   * @param in_w Input frame width in pixels.
   * @param in_h Input frame height in pixels.
   * @param out_w Output frame width in pixels.
   * @param out_h Output frame height in pixels.
   * @return Black luma and chroma values of the padded frame.
   */
  std::pair<int, int> init_padded_red_conversion(video::avcodec_software_encode_device_t &device, int in_w, int in_h, int out_w, int out_h) {
    setup_conversion(device, in_w, in_h, out_w, out_h, AV_PIX_FMT_YUV420P);

    const int black_luma = device.frame->data[0][0];
    const int black_chroma = device.frame->data[2][0];

    auto bgr0 = make_red_bgr0(in_w, in_h);
    platf::img_t img {};
    init_bgr0_img(img, bgr0, in_w, in_h);
    EXPECT_EQ(device.convert(img), 0);
    return {black_luma, black_chroma};
  }
}  // namespace

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
 * @brief Vertical letterbox padding keeps the rows above the content black and
 *        starts the scaled content at the odd luma offset (59 -> chroma 29).
 */
TEST(SoftwareEncoderConversion, PaddedFrameCopiesVerticalChroma) {
  constexpr int in_w = 320;  // exact 2x scale
  constexpr int in_h = 181;
  constexpr int out_w = 640;
  constexpr int out_h = 480;

  video::avcodec_software_encode_device_t device;
  const auto [black_luma, black_chroma] = init_padded_red_conversion(device, in_w, in_h, out_w, out_h);

  // Exact 2x scale: 640x362 content. The odd offset (59) is aligned down to 58
  // so the chroma grid stays aligned with the content.
  constexpr int content_top = 58;
  constexpr int content_bottom = content_top + 2 * in_h - 1;
  constexpr int chroma_top = content_top / 2;
  constexpr int chroma_bottom = chroma_top + in_h - 1;

  const auto *y_plane = device.frame->data[0];
  const auto *v_plane = device.frame->data[2];
  const int y_stride = device.frame->linesize[0];
  const int v_stride = device.frame->linesize[2];

  EXPECT_EQ(y_plane[(content_top - 1) * y_stride], black_luma) << "luma row above the content must stay padding";
  EXPECT_NE(y_plane[content_top * y_stride], black_luma) << "first content luma row must be copied";
  EXPECT_NE(y_plane[content_bottom * y_stride], black_luma) << "last content luma row must be copied";
  EXPECT_EQ(y_plane[(content_bottom + 1) * y_stride], black_luma) << "luma row below the content must stay padding";

  EXPECT_EQ(v_plane[(chroma_top - 1) * v_stride], black_chroma) << "chroma row 28 must stay padding";
  EXPECT_NE(v_plane[chroma_top * v_stride], black_chroma) << "chroma row 29 must receive content";
  EXPECT_NE(v_plane[chroma_bottom * v_stride], black_chroma) << "last content chroma row must be copied";
  EXPECT_EQ(v_plane[(chroma_bottom + 1) * v_stride], black_chroma) << "chroma row 210 must stay padding";
}

/**
 * @brief Odd scaled extents still copy the last chroma row and column into the
 *        padded frame (ceil-sized chroma planes).
 */
TEST(SoftwareEncoderConversion, PaddedFrameCopiesOddExtents) {
  constexpr int in_w = 321;  // odd capture width makes the scaled extent odd
  constexpr int in_h = 241;
  constexpr int out_w = 640;
  constexpr int out_h = 480;

  video::avcodec_software_encode_device_t device;
  setup_conversion(device, in_w, in_h, out_w, out_h, AV_PIX_FMT_YUV420P);

  const int black_luma = device.frame->data[0][out_w - 1];
  const int black_chroma = device.frame->data[2][0];

  auto bgr0 = make_red_bgr0(in_w, in_h);
  platf::img_t img {};
  init_bgr0_img(img, bgr0, in_w, in_h);
  ASSERT_EQ(device.convert(img), 0);

  // Mirror the device's scaling math, including the chroma-grid alignment.
  const auto scalar = std::fminf(static_cast<float>(out_w) / in_w, static_cast<float>(out_h) / in_h);
  const auto scaled_w = static_cast<int>(in_w * scalar);
  const auto scaled_h = static_cast<int>(in_h * scalar);
  const int offset_x = ((out_w - scaled_w) / 2) & ~1;
  const int offset_y = ((out_h - scaled_h) / 2) & ~1;
  const int chroma_x = offset_x / 2 + (scaled_w + 1) / 2 - 1;
  const int chroma_y = offset_y / 2 + (scaled_h + 1) / 2 - 1;

  const auto *v_plane = device.frame->data[2];
  const int v_stride = device.frame->linesize[2];
  EXPECT_NE(v_plane[0], black_chroma) << "scaled content must be copied";
  EXPECT_NE(v_plane[chroma_y * v_stride + chroma_x], black_chroma) << "last chroma row/column must be copied";
  EXPECT_EQ(device.frame->data[0][out_w - 1], black_luma) << "the rightmost luma column must remain padding";
}

/**
 * @brief Horizontal pillarbox padding places the content on the chroma grid
 *        and keeps the remaining columns as padding.
 */
TEST(SoftwareEncoderConversion, PaddedFrameCopiesHorizontalChroma) {
  constexpr int in_w = 319;  // 2x scale leaves an odd horizontal offset of 1
  constexpr int in_h = 240;
  constexpr int out_w = 640;
  constexpr int out_h = 480;

  video::avcodec_software_encode_device_t device;
  const auto [black_luma, black_chroma] = init_padded_red_conversion(device, in_w, in_h, out_w, out_h);

  // Exact 2x scale: 638x480 content. The odd offset (1) is aligned down to 0
  // so the chroma grid stays aligned with the content.
  constexpr int content_left = 0;
  constexpr int content_right = content_left + 2 * in_w - 1;
  constexpr int content_row = out_h / 2;

  const auto *y_plane = device.frame->data[0];
  const auto *v_plane = device.frame->data[2];
  const int y_stride = device.frame->linesize[0];
  const int v_stride = device.frame->linesize[2];

  EXPECT_NE(y_plane[content_row * y_stride + content_left], black_luma) << "first content luma column must be copied";
  EXPECT_NE(y_plane[content_row * y_stride + content_right], black_luma) << "last content luma column must be copied";
  EXPECT_EQ(y_plane[content_row * y_stride + content_right + 1], black_luma) << "luma column 638 must stay padding";
  EXPECT_EQ(y_plane[content_row * y_stride + content_right + 2], black_luma) << "luma column 639 must stay padding";

  // The aligned offset keeps the chroma grid aligned, and the scaled content
  // covers ceil(638 / 2) = 319 chroma columns.
  constexpr int chroma_row = out_h / 4;
  constexpr int content_chroma_width = (2 * in_w + 1) / 2;
  EXPECT_NE(v_plane[chroma_row * v_stride], black_chroma) << "chroma column 0 must receive content";
  EXPECT_NE(v_plane[chroma_row * v_stride + content_chroma_width - 1], black_chroma) << "last content chroma column must be copied";
  EXPECT_EQ(v_plane[chroma_row * v_stride + content_chroma_width], black_chroma) << "the last chroma column must remain padding";
}

/**
 * @brief Padded copies honor the 2-byte component step used by 10-bit output
 *        formats.
 */
TEST(SoftwareEncoderConversion, PaddedFrameCopiesTenBitComponents) {
  constexpr int in_w = 320;  // exact 2x scale
  constexpr int in_h = 181;
  constexpr int out_w = 640;
  constexpr int out_h = 480;

  video::avcodec_software_encode_device_t device;
  setup_conversion(device, in_w, in_h, out_w, out_h, AV_PIX_FMT_YUV420P10);

  const auto *y_plane = static_cast<const std::uint16_t *>(static_cast<const void *>(device.frame->data[0]));
  const auto *v_plane = static_cast<const std::uint16_t *>(static_cast<const void *>(device.frame->data[2]));
  const int y_stride = device.frame->linesize[0] / 2;
  const int v_stride = device.frame->linesize[2] / 2;
  const std::uint16_t black_luma = y_plane[0];
  const std::uint16_t black_chroma = v_plane[0];

  // Saturated red everywhere, so every chroma sample differs from black.
  auto bgr0 = make_red_bgr0(in_w, in_h);
  platf::img_t img {};
  init_bgr0_img(img, bgr0, in_w, in_h);
  ASSERT_EQ(device.convert(img), 0);

  // Exact 2x scale: 640x362 content. The odd offset (59) is aligned down to 58
  // so the chroma grid stays aligned with the content.
  constexpr int content_top = 58;
  constexpr int content_bottom = content_top + 2 * in_h - 1;
  constexpr int chroma_top = content_top / 2;
  constexpr int chroma_bottom = chroma_top + in_h - 1;

  EXPECT_EQ(y_plane[(content_top - 1) * y_stride], black_luma) << "luma row above the content must stay padding";
  EXPECT_NE(y_plane[content_top * y_stride], black_luma) << "first content luma row must be copied";
  EXPECT_NE(y_plane[content_bottom * y_stride], black_luma) << "last content luma row must be copied";
  EXPECT_EQ(y_plane[(content_bottom + 1) * y_stride], black_luma) << "luma row below the content must stay padding";

  EXPECT_EQ(v_plane[(chroma_top - 1) * v_stride], black_chroma) << "chroma row 28 must stay padding";
  EXPECT_NE(v_plane[chroma_top * v_stride], black_chroma) << "chroma row 29 must receive content";
  EXPECT_NE(v_plane[chroma_bottom * v_stride], black_chroma) << "last content chroma row must be copied";
}

namespace {
  constexpr int kConversionWidth = 320;
  constexpr int kConversionHeight = 240;

  /**
   * @brief Capture the converted YUV420P planes of the software device.
   *
   * @param device Software encode device holding the converted frame.
   * @return Plane bytes in Y, U, V order.
   */
  std::vector<std::uint8_t> snapshot_conversion(const video::avcodec_software_encode_device_t &device) {
    std::vector<std::uint8_t> out;
    const auto fmt = static_cast<AVPixelFormat>(device.frame->format);
    const auto *desc = av_pix_fmt_desc_get(fmt);
    const int plane_count = av_pix_fmt_count_planes(fmt);
    for (int plane = 0; plane < plane_count; ++plane) {
      const int shift_w = (plane == 0) ? 0 : desc->log2_chroma_w;
      const int shift_h = (plane == 0) ? 0 : desc->log2_chroma_h;
      const int plane_w = kConversionWidth >> shift_w;
      const int plane_h = kConversionHeight >> shift_h;
      for (int y = 0; y < plane_h; ++y) {
        const auto *row = device.frame->data[plane] + static_cast<std::ptrdiff_t>(y) * device.frame->linesize[plane];
        out.insert(out.end(), row, row + plane_w);
      }
    }
    return out;
  }

  /**
   * @brief Mean of a byte range inside a snapshot.
   *
   * @param data Snapshot bytes.
   * @param begin First byte index.
   * @param end One past the last byte index.
   * @return Arithmetic mean of the range.
   */
  double plane_mean(const std::vector<std::uint8_t> &data, std::size_t begin, std::size_t end) {
    double sum = 0;
    for (std::size_t i = begin; i < end; ++i) {
      sum += data[i];
    }
    return sum / static_cast<double>(end - begin);
  }

  /**
   * @brief Compare two snapshots with a rounding tolerance.
   *
   * Different source formats may take slightly different swscale paths, so
   * cross-format comparisons are not expected to be bit-exact.
   *
   * @param actual Converted snapshot under test.
   * @param expected Reference snapshot.
   * @param tolerance Maximum accepted per-byte difference.
   * @param label Description used in failure messages.
   */
  void expect_snapshots_near(const std::vector<std::uint8_t> &actual, const std::vector<std::uint8_t> &expected, int tolerance, const char *label) {
    ASSERT_EQ(actual.size(), expected.size()) << label;
    for (std::size_t i = 0; i < expected.size(); ++i) {
      const int diff = actual[i] > expected[i] ? actual[i] - expected[i] : expected[i] - actual[i];
      EXPECT_LE(diff, tolerance) << label << " byte " << i;
    }
  }

  /**
   * @brief Convert an encoded 2D color pattern through the software device.
   *
   * @tparam EncodeFn Callable returning the packed pixel for a coordinate.
   * @param device Software encode device.
   * @param pixel_format Declared capture format of the pattern.
   * @param encode Pixel encoder taking x and y coordinates.
   * @return Converted snapshot.
   */
  template<typename EncodeFn>
  std::vector<std::uint8_t> convert_pattern(video::avcodec_software_encode_device_t &device, platf::pix_fmt_e pixel_format, EncodeFn encode) {
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
    for (int y = 0; y < kConversionHeight; ++y) {
      for (int x = 0; x < kConversionWidth; ++x) {
        const std::uint32_t pixel = encode(x, y);
        const std::size_t offset = (static_cast<std::size_t>(y) * kConversionWidth + x) * 4;
        for (int byte = 0; byte < 4; ++byte) {
          buffer[offset + byte] = static_cast<std::uint8_t>(pixel >> (8 * byte));
        }
      }
    }

    platf::img_t img {};
    img.data = buffer.data();
    img.width = kConversionWidth;
    img.height = kConversionHeight;
    img.row_pitch = kConversionWidth * 4;
    img.pixel_pitch = 4;
    img.pixel_format = pixel_format;
    const int status = device.convert(img);
    EXPECT_EQ(status, 0);
    if (status != 0) {
      return {};
    }
    return snapshot_conversion(device);
  }
}  // namespace

/**
 * @brief Software encoder converts packed BGR0 and planar NV12 captures,
 *        including padded strides and explicitly declared formats.
 */
TEST(SoftwareEncoderConversion, ConvertsPackedAndPlanarLayouts) {
  video::avcodec_software_encode_device_t device;
  setup_conversion(device, kConversionWidth, kConversionHeight, kConversionWidth, kConversionHeight, AV_PIX_FMT_YUV420P);

  // BGR0 frame (4 bytes per pixel) -- the classic KMS/DMABUF capture layout.
  std::vector<std::uint8_t> bgr0_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t bgr0_img {};
  bgr0_img.data = bgr0_buffer.data();
  bgr0_img.width = kConversionWidth;
  bgr0_img.height = kConversionHeight;
  bgr0_img.row_pitch = kConversionWidth * 4;
  bgr0_img.pixel_pitch = 4;
  EXPECT_EQ(device.convert(bgr0_img), 0);

  // NV12 frame (1 byte per pixel row pitch, Y plane + interleaved UV) -- the
  // layout delivered by PipeWire-based captures (KWin screencast / portal).
  std::vector<std::uint8_t> nv12_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 3 / 2);
  platf::img_t nv12_img {};
  nv12_img.data = nv12_buffer.data();
  nv12_img.width = kConversionWidth;
  nv12_img.height = kConversionHeight;
  nv12_img.row_pitch = kConversionWidth;
  nv12_img.pixel_pitch = 1;
  EXPECT_EQ(device.convert(nv12_img), 0);

  // Tightly packed and padded NV12 frames with the same content must convert
  // identically, which verifies the declared row pitch is honored.
  auto make_nv12_pattern = [](int stride) {
    std::vector<std::uint8_t> data(static_cast<std::size_t>(stride) * kConversionHeight + static_cast<std::size_t>(stride) * kConversionHeight / 2);
    for (int y = 0; y < kConversionHeight; ++y) {
      for (int x = 0; x < kConversionWidth; ++x) {
        data[static_cast<std::size_t>(y) * stride + x] = static_cast<std::uint8_t>(x * 255 / (kConversionWidth - 1));
      }
    }
    for (int y = 0; y < kConversionHeight / 2; ++y) {
      for (int x = 0; x < kConversionWidth; ++x) {
        data[static_cast<std::size_t>(stride) * kConversionHeight + static_cast<std::size_t>(y) * stride + x] = static_cast<std::uint8_t>(y * 255 / (kConversionHeight / 2 - 1));
      }
    }
    return data;
  };

  constexpr int padded_stride = kConversionWidth + 32;
  std::vector<std::uint8_t> tight_nv12_buffer = make_nv12_pattern(kConversionWidth);
  std::vector<std::uint8_t> padded_nv12_buffer = make_nv12_pattern(padded_stride);

  platf::img_t tight_nv12_img {};
  tight_nv12_img.data = tight_nv12_buffer.data();
  tight_nv12_img.width = kConversionWidth;
  tight_nv12_img.height = kConversionHeight;
  tight_nv12_img.row_pitch = kConversionWidth;
  tight_nv12_img.pixel_pitch = 1;
  EXPECT_EQ(device.convert(tight_nv12_img), 0);
  const auto tight_nv12_snapshot = snapshot_conversion(device);

  platf::img_t padded_nv12_img {};
  padded_nv12_img.data = padded_nv12_buffer.data();
  padded_nv12_img.width = kConversionWidth;
  padded_nv12_img.height = kConversionHeight;
  padded_nv12_img.row_pitch = padded_stride;
  padded_nv12_img.pixel_pitch = 1;
  EXPECT_EQ(device.convert(padded_nv12_img), 0);
  EXPECT_EQ(snapshot_conversion(device), tight_nv12_snapshot) << "padded NV12 rows must be read with the declared row pitch";

  // Capture backends that declare the negotiated format explicitly take
  // precedence over the pixel_pitch heuristic.
  platf::img_t explicit_bgr0_img {};
  explicit_bgr0_img.data = bgr0_buffer.data();
  explicit_bgr0_img.width = kConversionWidth;
  explicit_bgr0_img.height = kConversionHeight;
  explicit_bgr0_img.row_pitch = kConversionWidth * 4;
  explicit_bgr0_img.pixel_pitch = 4;
  explicit_bgr0_img.pixel_format = platf::pix_fmt_e::bgr0;
  EXPECT_EQ(device.convert(explicit_bgr0_img), 0);

  // NV12 declared explicitly -- even with a misleading pixel_pitch the declared
  // format wins and the interleaved UV plane is read correctly. Changing only
  // the chroma plane must change the converted chroma output.
  std::vector<std::uint8_t> explicit_nv12_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 3 / 2, 0x40);
  const std::size_t nv12_y_size = static_cast<std::size_t>(kConversionWidth) * kConversionHeight;
  const std::size_t nv12_uv_size = nv12_y_size / 2;

  platf::img_t explicit_nv12_img {};
  explicit_nv12_img.data = explicit_nv12_buffer.data();
  explicit_nv12_img.width = kConversionWidth;
  explicit_nv12_img.height = kConversionHeight;
  explicit_nv12_img.row_pitch = kConversionWidth;
  explicit_nv12_img.pixel_pitch = 4;
  explicit_nv12_img.pixel_format = platf::pix_fmt_e::nv12;
  EXPECT_EQ(device.convert(explicit_nv12_img), 0);
  const auto nv12_low_chroma = snapshot_conversion(device);

  std::fill(explicit_nv12_buffer.begin() + static_cast<std::ptrdiff_t>(nv12_y_size), explicit_nv12_buffer.end(), 0xC0);
  EXPECT_EQ(device.convert(explicit_nv12_img), 0);
  const auto nv12_high_chroma = snapshot_conversion(device);

  EXPECT_NE(nv12_low_chroma, nv12_high_chroma) << "declared NV12 must read the interleaved UV plane";
  EXPECT_GT(
    plane_mean(nv12_high_chroma, nv12_y_size, nv12_y_size + nv12_uv_size),
    plane_mean(nv12_low_chroma, nv12_y_size, nv12_y_size + nv12_uv_size)
  );
}

/**
 * @brief Software encoder accepts the declared packed RGB capture formats,
 *        including 10-bit layouts with padded strides.
 */
TEST(SoftwareEncoderConversion, ConvertsDeclaredPackedFormats) {
  video::avcodec_software_encode_device_t device;
  setup_conversion(device, kConversionWidth, kConversionHeight, kConversionWidth, kConversionHeight, AV_PIX_FMT_YUV420P);

  auto init_image = [](platf::img_t &img, std::vector<std::uint8_t> &buffer, platf::pix_fmt_e format) {
    img.data = buffer.data();
    img.width = kConversionWidth;
    img.height = kConversionHeight;
    img.row_pitch = kConversionWidth * 4;
    img.pixel_pitch = 4;
    img.pixel_format = format;
  };

  // BGRA capture (alpha channel present) and RGBA capture (R,G,B,A byte order,
  // e.g. Windows R8G8B8A8) -- both declared explicitly.
  std::vector<std::uint8_t> bgra_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t bgra_img {};
  init_image(bgra_img, bgra_buffer, platf::pix_fmt_e::bgra);
  EXPECT_EQ(device.convert(bgra_img), 0);

  std::vector<std::uint8_t> rgba_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t rgba_img {};
  init_image(rgba_img, rgba_buffer, platf::pix_fmt_e::rgba);
  EXPECT_EQ(device.convert(rgba_img), 0);

  // Packed 10-bit RGB (e.g. XBGR2101010 from an HDR PipeWire capture). The
  // pattern depends on both coordinates, so a row or column mix-up changes the
  // converted output instead of only a uniform color.
  constexpr int padded10_stride = kConversionWidth * 4 + 32;
  auto make_xbgr10_pattern = [](int stride) {
    std::vector<std::uint8_t> data(static_cast<std::size_t>(stride) * kConversionHeight);
    for (int y = 0; y < kConversionHeight; ++y) {
      for (int x = 0; x < kConversionWidth; ++x) {
        // xBGR2101010: X:B:G:R, R depends on x and B on y.
        const auto red = static_cast<std::uint32_t>(x * 1023 / (kConversionWidth - 1));
        const auto blue = static_cast<std::uint32_t>(y * 1023 / (kConversionHeight - 1));
        const std::uint32_t pixel = red | (blue << 20);
        const std::size_t offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
        for (int byte = 0; byte < 4; ++byte) {
          data[offset + byte] = static_cast<std::uint8_t>(pixel >> (8 * byte));
        }
      }
    }
    return data;
  };

  std::vector<std::uint8_t> tight_xbgr10_buffer = make_xbgr10_pattern(kConversionWidth * 4);
  platf::img_t tight_xbgr10_img {};
  init_image(tight_xbgr10_img, tight_xbgr10_buffer, platf::pix_fmt_e::xbgr2101010);
  EXPECT_EQ(device.convert(tight_xbgr10_img), 0);
  const auto tight_xbgr10_snapshot = snapshot_conversion(device);

  // A padded frame with the same content must convert identically.
  std::vector<std::uint8_t> padded_xbgr10_buffer = make_xbgr10_pattern(padded10_stride);
  platf::img_t padded_xbgr10_img {};
  init_image(padded_xbgr10_img, padded_xbgr10_buffer, platf::pix_fmt_e::xbgr2101010);
  padded_xbgr10_img.row_pitch = padded10_stride;
  EXPECT_EQ(device.convert(padded_xbgr10_img), 0);
  EXPECT_EQ(snapshot_conversion(device), tight_xbgr10_snapshot) << "padded 10-bit rows must be read with the declared row pitch";

  // 10-bit with alpha in high bits -- bit-identical to X2RGB10LE/X2BGR10LE.
  std::vector<std::uint8_t> abgr10_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t abgr10_img {};
  init_image(abgr10_img, abgr10_buffer, platf::pix_fmt_e::abgr2101010);
  EXPECT_EQ(device.convert(abgr10_img), 0);

  std::vector<std::uint8_t> argb10_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t argb10_img {};
  init_image(argb10_img, argb10_buffer, platf::pix_fmt_e::argb2101010);
  EXPECT_EQ(device.convert(argb10_img), 0);

  // 10-bit with alpha in low bits (BGRA1010102/RGBA1010102), provided by the
  // patched FFmpeg from build-deps.
  std::vector<std::uint8_t> bgra1010102_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t bgra1010102_img {};
  init_image(bgra1010102_img, bgra1010102_buffer, platf::pix_fmt_e::bgra1010102);
  EXPECT_EQ(device.convert(bgra1010102_img), 0);

  std::vector<std::uint8_t> rgba1010102_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t rgba1010102_img {};
  init_image(rgba1010102_img, rgba1010102_buffer, platf::pix_fmt_e::rgba1010102);
  EXPECT_EQ(device.convert(rgba1010102_img), 0);
}

/**
 * @brief The same 2D color pattern stored in different byte orders converts to
 *        the same output, which catches swapped channels, rows or columns.
 */
TEST(SoftwareEncoderConversion, ComparesEquivalentColorPatterns) {
  video::avcodec_software_encode_device_t device;
  setup_conversion(device, kConversionWidth, kConversionHeight, kConversionWidth, kConversionHeight, AV_PIX_FMT_YUV420P);

  const auto red8 = [](int x) {
    return static_cast<std::uint32_t>(x * 255 / (kConversionWidth - 1));
  };
  const auto blue8 = [](int y) {
    return static_cast<std::uint32_t>(y * 255 / (kConversionHeight - 1));
  };
  const auto red10 = [](int x) {
    return static_cast<std::uint32_t>(x * 1023 / (kConversionWidth - 1));
  };
  const auto blue10 = [](int y) {
    return static_cast<std::uint32_t>(y * 1023 / (kConversionHeight - 1));
  };

  // 8-bit: BGR0/BGRA store B first, RGBA stores R first.
  const auto reference_8bit = convert_pattern(device, platf::pix_fmt_e::bgr0, [&red8, &blue8](int x, int y) {
    return blue8(y) | (red8(x) << 16);
  });
  expect_snapshots_near(convert_pattern(device, platf::pix_fmt_e::bgra, [&red8, &blue8](int x, int y) {
                          return blue8(y) | (red8(x) << 16) | 0xFF000000u;
                        }),
                        reference_8bit,
                        16,
                        "BGRA vs BGR0");
  expect_snapshots_near(convert_pattern(device, platf::pix_fmt_e::rgba, [&red8, &blue8](int x, int y) {
                          return red8(x) | (blue8(y) << 16) | 0xFF000000u;
                        }),
                        reference_8bit,
                        16,
                        "RGBA vs BGR0");

  // 10-bit: X2BGR10 stores R in the low bits, X2RGB10 in the high bits.
  const auto reference_x2bgr10 = convert_pattern(device, platf::pix_fmt_e::xbgr2101010, [&red10, &blue10](int x, int y) {
    return red10(x) | (blue10(y) << 20);
  });
  // Comparing against the 8-bit reference catches R/B swaps that a comparison
  // between 10-bit pairs alone would not.
  expect_snapshots_near(reference_x2bgr10, reference_8bit, 24, "X2BGR10 vs BGR0");
  expect_snapshots_near(convert_pattern(device, platf::pix_fmt_e::argb2101010, [&red10, &blue10](int x, int y) {
                          return blue10(y) | (red10(x) << 20) | (0x3u << 30);
                        }),
                        reference_x2bgr10,
                        2,
                        "X2RGB10 vs X2BGR10");

  // ABGR2101010 shares the X2BGR10 layout; only the high 2 bits are padding.
  expect_snapshots_near(convert_pattern(device, platf::pix_fmt_e::abgr2101010, [&red10, &blue10](int x, int y) {
                          return red10(x) | (blue10(y) << 20) | (0x3u << 30);
                        }),
                        reference_x2bgr10,
                        2,
                        "ABGR2101010 vs X2BGR10");

  // 10:10:10:2 with the alpha in the low bits.
  const auto reference_1010102 = convert_pattern(device, platf::pix_fmt_e::bgra1010102, [&red10, &blue10](int x, int y) {
    return (blue10(y) << 22) | (red10(x) << 2) | 3u;
  });
  expect_snapshots_near(reference_1010102, reference_8bit, 24, "BGRA1010102 vs BGR0");
  expect_snapshots_near(convert_pattern(device, platf::pix_fmt_e::rgba1010102, [&red10, &blue10](int x, int y) {
                          return (red10(x) << 22) | (blue10(y) << 2) | 3u;
                        }),
                        reference_1010102,
                        2,
                        "RGBA1010102 vs BGRA1010102");
}

/**
 * @brief Declared semi-planar P010 captures read the chroma plane that follows
 *        the luma plane.
 */
TEST(SoftwareEncoderConversion, ConvertsDeclaredP010Layout) {
  video::avcodec_software_encode_device_t device;
  setup_conversion(device, kConversionWidth, kConversionHeight, kConversionWidth, kConversionHeight, AV_PIX_FMT_YUV420P);

  // P010 (10-bit YUV semi-planar) -- used on macOS. Luma is the first w*h*2
  // bytes and the chroma plane follows; changing only the chroma plane must
  // change the converted chroma output.
  std::vector<std::uint8_t> p010_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 3, 0x40);
  const std::size_t p010_y_size = static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 2;
  const std::size_t p010_yuv_size = static_cast<std::size_t>(kConversionWidth) * kConversionHeight;
  const std::size_t p010_uv_size = p010_yuv_size / 2;

  platf::img_t p010_img {};
  p010_img.data = p010_buffer.data();
  p010_img.width = kConversionWidth;
  p010_img.height = kConversionHeight;
  p010_img.row_pitch = kConversionWidth * 2;
  p010_img.pixel_pitch = 2;
  p010_img.pixel_format = platf::pix_fmt_e::p010;
  EXPECT_EQ(device.convert(p010_img), 0);
  const auto p010_low_chroma = snapshot_conversion(device);

  std::fill(p010_buffer.begin() + static_cast<std::ptrdiff_t>(p010_y_size), p010_buffer.end(), 0xC0);
  EXPECT_EQ(device.convert(p010_img), 0);
  const auto p010_high_chroma = snapshot_conversion(device);

  EXPECT_NE(p010_low_chroma, p010_high_chroma) << "declared P010 must read the chroma plane";
  EXPECT_GT(
    plane_mean(p010_high_chroma, p010_yuv_size, p010_yuv_size + p010_uv_size),
    plane_mean(p010_low_chroma, p010_yuv_size, p010_yuv_size + p010_uv_size)
  );
}

/**
 * @brief Undeclared captures with the historical layouts are still converted.
 */
TEST(SoftwareEncoderConversion, ConvertsUndeclaredFallbackLayouts) {
  video::avcodec_software_encode_device_t device;
  setup_conversion(device, kConversionWidth, kConversionHeight, kConversionWidth, kConversionHeight, AV_PIX_FMT_YUV420P);

  // Capture backends that don't report pixel_pitch fall back to deriving it
  // from the row pitch (1 byte per pixel = NV12, 4 = BGR0).
  std::vector<std::uint8_t> bgr0_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);
  platf::img_t fallback_bgr0_img {};
  fallback_bgr0_img.data = bgr0_buffer.data();
  fallback_bgr0_img.width = kConversionWidth;
  fallback_bgr0_img.height = kConversionHeight;
  fallback_bgr0_img.row_pitch = kConversionWidth * 4;
  EXPECT_EQ(device.convert(fallback_bgr0_img), 0);

  std::vector<std::uint8_t> nv12_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 3 / 2);
  platf::img_t fallback_nv12_img {};
  fallback_nv12_img.data = nv12_buffer.data();
  fallback_nv12_img.width = kConversionWidth;
  fallback_nv12_img.height = kConversionHeight;
  fallback_nv12_img.row_pitch = kConversionWidth;
  EXPECT_EQ(device.convert(fallback_nv12_img), 0);
}

/**
 * @brief Unsupported declared formats and ambiguous undeclared layouts fail
 *        loudly instead of being misinterpreted.
 */
TEST(SoftwareEncoderConversion, RejectsUnsupportedAndAmbiguousLayouts) {
  video::avcodec_software_encode_device_t device;
  setup_conversion(device, kConversionWidth, kConversionHeight, kConversionWidth, kConversionHeight, AV_PIX_FMT_YUV420P);

  std::vector<std::uint8_t> bgr0_buffer(static_cast<std::size_t>(kConversionWidth) * kConversionHeight * 4);

  // A declared format with no capture mapping must fail loudly instead of
  // falling back to the pixel pitch heuristic.
  platf::img_t unsupported_img {};
  unsupported_img.data = bgr0_buffer.data();
  unsupported_img.width = kConversionWidth;
  unsupported_img.height = kConversionHeight;
  unsupported_img.row_pitch = kConversionWidth * 4;
  unsupported_img.pixel_pitch = 4;
  unsupported_img.pixel_format = platf::pix_fmt_e::yuv420p;
  EXPECT_NE(device.convert(unsupported_img), 0);

  // A declared packed BGR format whose reported pitch is inconsistent would be
  // scaled at the wrong stride and must be rejected instead.
  platf::img_t inconsistent_bgr_img {};
  inconsistent_bgr_img.data = bgr0_buffer.data();
  inconsistent_bgr_img.width = kConversionWidth;
  inconsistent_bgr_img.height = kConversionHeight;
  inconsistent_bgr_img.row_pitch = kConversionWidth * 3;
  inconsistent_bgr_img.pixel_pitch = 3;
  inconsistent_bgr_img.pixel_format = platf::pix_fmt_e::bgr0;
  EXPECT_NE(device.convert(inconsistent_bgr_img), 0);

  // Declared packed 4-byte and semi-planar formats must describe complete rows
  // too; otherwise the scaler would read them at the wrong stride.
  platf::img_t short_rgba_img {};
  short_rgba_img.data = bgr0_buffer.data();
  short_rgba_img.width = kConversionWidth;
  short_rgba_img.height = kConversionHeight;
  short_rgba_img.row_pitch = kConversionWidth * 4 - 4;
  short_rgba_img.pixel_pitch = 4;
  short_rgba_img.pixel_format = platf::pix_fmt_e::rgba;
  EXPECT_NE(device.convert(short_rgba_img), 0);

  platf::img_t short_nv12_img {};
  short_nv12_img.data = bgr0_buffer.data();
  short_nv12_img.width = kConversionWidth;
  short_nv12_img.height = kConversionHeight;
  short_nv12_img.row_pitch = kConversionWidth - 4;
  short_nv12_img.pixel_pitch = 1;
  short_nv12_img.pixel_format = platf::pix_fmt_e::nv12;
  EXPECT_NE(device.convert(short_nv12_img), 0);

  // A semi-planar row pitch must be even so chroma sample pairs stay aligned;
  // an odd pitch would read U/V one byte off.
  platf::img_t odd_nv12_img {};
  odd_nv12_img.data = bgr0_buffer.data();
  odd_nv12_img.width = kConversionWidth;
  odd_nv12_img.height = kConversionHeight;
  odd_nv12_img.row_pitch = kConversionWidth + 1;
  odd_nv12_img.pixel_pitch = 1;
  odd_nv12_img.pixel_format = platf::pix_fmt_e::nv12;
  EXPECT_NE(device.convert(odd_nv12_img), 0);

  platf::img_t short_p010_img {};
  short_p010_img.data = bgr0_buffer.data();
  short_p010_img.width = kConversionWidth;
  short_p010_img.height = kConversionHeight;
  short_p010_img.row_pitch = kConversionWidth * 2 - 4;
  short_p010_img.pixel_pitch = 2;
  short_p010_img.pixel_format = platf::pix_fmt_e::p010;
  EXPECT_NE(device.convert(short_p010_img), 0);

  // The undeclared NV12 fallback must describe complete luma rows too.
  platf::img_t short_undeclared_nv12_img {};
  short_undeclared_nv12_img.data = bgr0_buffer.data();
  short_undeclared_nv12_img.width = kConversionWidth;
  short_undeclared_nv12_img.height = kConversionHeight;
  short_undeclared_nv12_img.row_pitch = kConversionWidth - 4;
  short_undeclared_nv12_img.pixel_pitch = 1;
  short_undeclared_nv12_img.pixel_format = platf::pix_fmt_e::unknown;
  EXPECT_NE(device.convert(short_undeclared_nv12_img), 0);

  // A declared P010 row pitch must hold whole 2-byte samples.
  platf::img_t odd_p010_img {};
  odd_p010_img.data = bgr0_buffer.data();
  odd_p010_img.width = kConversionWidth;
  odd_p010_img.height = kConversionHeight;
  odd_p010_img.row_pitch = kConversionWidth * 2 + 1;
  odd_p010_img.pixel_pitch = 2;
  odd_p010_img.pixel_format = platf::pix_fmt_e::p010;
  EXPECT_NE(device.convert(odd_p010_img), 0);

  // Undeclared captures with an ambiguous pitch must fail instead of being
  // decoded as packed BGR0 (e.g. 2-byte RGB565 or 8-byte scRGB).
  platf::img_t fallback_rgb565_img {};
  fallback_rgb565_img.data = bgr0_buffer.data();
  fallback_rgb565_img.width = kConversionWidth;
  fallback_rgb565_img.height = kConversionHeight;
  fallback_rgb565_img.row_pitch = kConversionWidth * 2;
  EXPECT_NE(device.convert(fallback_rgb565_img), 0);

  platf::img_t fallback_bgr24_img {};
  fallback_bgr24_img.data = bgr0_buffer.data();
  fallback_bgr24_img.width = kConversionWidth;
  fallback_bgr24_img.height = kConversionHeight;
  fallback_bgr24_img.row_pitch = kConversionWidth * 3;
  EXPECT_NE(device.convert(fallback_bgr24_img), 0);

  platf::img_t fallback_scrgb_img {};
  fallback_scrgb_img.data = bgr0_buffer.data();
  fallback_scrgb_img.width = kConversionWidth;
  fallback_scrgb_img.height = kConversionHeight;
  fallback_scrgb_img.row_pitch = kConversionWidth * 8;
  EXPECT_NE(device.convert(fallback_scrgb_img), 0);

  // An odd P010 width needs one more chroma pair than the luma row: a pitch
  // that exactly fits the luma row is insufficient for the chroma pairs and
  // must be rejected before the scaler reads past the row.
  platf::img_t tight_odd_p010_img {};
  tight_odd_p010_img.data = bgr0_buffer.data();
  tight_odd_p010_img.width = kConversionWidth + 1;
  tight_odd_p010_img.height = kConversionHeight;
  tight_odd_p010_img.row_pitch = (kConversionWidth + 1) * 2;
  tight_odd_p010_img.pixel_pitch = 2;
  tight_odd_p010_img.pixel_format = platf::pix_fmt_e::p010;
  EXPECT_NE(device.convert(tight_odd_p010_img), 0);

  // Invalid image pointers or heights must be rejected before any plane
  // pointer arithmetic or scaling.
  platf::img_t null_data_img {};
  null_data_img.data = nullptr;
  null_data_img.width = kConversionWidth;
  null_data_img.height = kConversionHeight;
  null_data_img.row_pitch = kConversionWidth * 4;
  null_data_img.pixel_format = platf::pix_fmt_e::bgr0;
  EXPECT_NE(device.convert(null_data_img), 0);

  platf::img_t zero_height_img {};
  zero_height_img.data = bgr0_buffer.data();
  zero_height_img.width = kConversionWidth;
  zero_height_img.height = 0;
  zero_height_img.row_pitch = kConversionWidth * 4;
  zero_height_img.pixel_format = platf::pix_fmt_e::bgr0;
  EXPECT_NE(device.convert(zero_height_img), 0);

  zero_height_img.height = -1;
  EXPECT_NE(device.convert(zero_height_img), 0);

  // A capture image whose size differs from the converter input must be
  // rejected before the scaler reads the planes at the wrong dimensions.
  platf::img_t wrong_size_img {};
  wrong_size_img.data = bgr0_buffer.data();
  wrong_size_img.width = kConversionWidth - 2;
  wrong_size_img.height = kConversionHeight;
  wrong_size_img.row_pitch = (kConversionWidth - 2) * 4;
  wrong_size_img.pixel_pitch = 4;
  wrong_size_img.pixel_format = platf::pix_fmt_e::bgr0;
  EXPECT_NE(device.convert(wrong_size_img), 0);
}
