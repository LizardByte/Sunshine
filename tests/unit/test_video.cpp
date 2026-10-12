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
#include <src/display_device.h>
#include <src/video.h>
#include <src/video_capture_pool.h>
#include <src/video_probe.h>

using namespace std::literals;

/** @brief Codec policy truth table distinguishes selection requirements from encoder-preferred fallback. */
TEST(VideoProbePolicyTest, PreservesCodecConstraintsAndAutomaticAdvertisement) {
  const std::array<std::array<bool, 4>, 8> requirements {{
    {false, false, false, false},
    {true, false, false, false},
    {false, false, false, false},
    {true, true, false, true},
    {false, false, false, false},
    {true, false, true, true},
    {false, false, false, false},
    {true, true, true, true},
  }};
  const std::array advertised {1, 2, 1, 3, 1, 4, 1, 5};
  for (unsigned int bits = 0; bits < requirements.size(); ++bits) {
    video::encoder_t::codec_t codec;
    codec[video::encoder_t::PASSED] = bits & 1;
    codec[video::encoder_t::DYNAMIC_RANGE] = bits & 2;
    codec[video::encoder_t::DYNAMIC_RANGE_YUV444] = bits & 4;
    EXPECT_EQ(video::detail::advertised_codec_mode(codec, 0), advertised[bits]);
    EXPECT_TRUE(video::detail::codec_meets_requirements(codec, 0));
    EXPECT_TRUE(video::detail::codec_meets_requirements(codec, 1));
    for (int mode = 2; mode <= 5; ++mode) {
      EXPECT_EQ(video::detail::codec_meets_requirements(codec, mode), requirements[bits][mode - 2]) << bits << ':' << mode;
      EXPECT_EQ(video::detail::advertised_codec_mode(codec, mode), mode);
    }
  }
  video::encoder_t::codec_t codec;
  EXPECT_EQ(video::detail::unsupported_codec_requirement(codec, 2), std::optional<std::string_view> {""});
  EXPECT_EQ(video::detail::unsupported_codec_requirement(codec, 3), std::optional<std::string_view> {"Main10"});
  EXPECT_EQ(video::detail::unsupported_codec_requirement(codec, 4), std::optional<std::string_view> {"Rext10_444"});
  EXPECT_EQ(video::detail::unsupported_codec_requirement(codec, 5), std::optional<std::string_view> {"Main10 Rext10_444"});
  EXPECT_FALSE(video::detail::unsupported_codec_requirement(codec, 0));
  EXPECT_FALSE(video::detail::unsupported_codec_requirement(codec, 6));
  codec[video::encoder_t::DYNAMIC_RANGE] = true;
  EXPECT_FALSE(video::detail::unsupported_codec_requirement(codec, 3));
  EXPECT_FALSE(video::detail::unsupported_codec_requirement(codec, 5));
  EXPECT_FALSE(video::detail::codec_meets_requirements(codec, 3));
}

/** @brief Optional codec probing preserves automatic, disabled and explicitly requested behavior. */
TEST(VideoProbePolicyTest, ProbesOnlyRequestedOrPermittedAutomaticCodecs) {
  for (bool h264_only : {false, true}) {
    EXPECT_FALSE(video::detail::should_probe_codec(-1, h264_only));
    EXPECT_EQ(video::detail::should_probe_codec(0, h264_only), !h264_only);
    EXPECT_FALSE(video::detail::should_probe_codec(1, h264_only));
    EXPECT_TRUE(video::detail::should_probe_codec(2, h264_only));
    EXPECT_TRUE(video::detail::should_probe_codec(5, h264_only));
  }
}

/** @brief Pixel probe configurations preserve all negotiated defaults while varying codec, HDR and chroma. */
TEST(VideoProbePolicyTest, PixelModeConfigurationsPreserveReferenceAndColorDefaults) {
  for (int format : {0, 1, 2}) {
    for (bool hdr : {false, true}) {
      for (bool yuv444 : {false, true}) {
        const auto config = video::detail::pixel_probe_config(format, hdr, yuv444);
        EXPECT_EQ(config.width, 1920);
        EXPECT_EQ(config.height, 1080);
        EXPECT_EQ(config.framerate, 60);
        EXPECT_EQ(config.framerateX100, 6000);
        EXPECT_EQ(config.bitrate, 1000);
        EXPECT_EQ(config.slicesPerFrame, 1);
        EXPECT_EQ(config.numRefFrames, 0);
        EXPECT_EQ(config.encoderCscMode, hdr ? 3 : 1);
        EXPECT_EQ(config.videoFormat, format);
        EXPECT_EQ(config.dynamicRange, hdr ? 1 : 0);
        EXPECT_EQ(config.chromaSamplingType, yuv444 ? 1 : 0);
        EXPECT_EQ(config.enableIntraRefresh, 0);
      }
    }
  }
}

/** @brief A full pool preserves outstanding frames and retries only one failed allocation per acquisition. */
TEST(VideoCapturePoolTest, PreservesCapacityAndDoesNotReuseExternallyRetainedImages) {
  video::detail::capture_image_pool_t pool {2};
  int allocations = 0;
  int clock_reads = 0;
  const auto allocate = [&allocations] {
    ++allocations;
    auto image = std::make_shared<platf::img_t>();
    image->frame_timestamp = std::chrono::steady_clock::time_point {};
    return image;
  };
  const auto clock = [&clock_reads] {
    ++clock_reads;
    return std::chrono::steady_clock::time_point {};
  };
  auto first = pool.acquire(allocate, clock);
  auto second = pool.acquire(allocate, clock);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first, second);
  EXPECT_FALSE(first->frame_timestamp);
  EXPECT_FALSE(pool.acquire(allocate, clock));
  EXPECT_EQ(allocations, 2);
  EXPECT_EQ(clock_reads, 2);
  const auto *first_pointer = first.get();
  first.reset();
  auto reused = pool.acquire(allocate, clock);
  EXPECT_EQ(reused.get(), first_pointer);
  EXPECT_EQ(allocations, 2);
  std::weak_ptr<platf::img_t> retained = second;
  pool.clear();
  EXPECT_FALSE(retained.expired());
  second.reset();
  EXPECT_TRUE(retained.expired());
  EXPECT_NE(pool.acquire(allocate, clock), nullptr);
  EXPECT_EQ(allocations, 3);
  video::detail::capture_image_pool_t empty {0};
  EXPECT_FALSE(empty.acquire(allocate, clock));
  video::detail::capture_image_pool_t failing {2};
  int failures = 0;
  const auto fail = [&failures]() -> std::shared_ptr<platf::img_t> {
    ++failures;
    return {};
  };
  EXPECT_FALSE(failing.acquire(fail, clock));
  EXPECT_EQ(failures, 1);
  EXPECT_EQ(clock_reads, 4);
}

/** @brief Idle trimming retains recent peak demand for three seconds and frees the least recently selected images. */
TEST(VideoCapturePoolTest, TrimsIdleImagesAtTheDemandTimeoutWithoutReleasingUsedFrames) {
  video::detail::capture_image_pool_t pool {3};
  auto now = std::chrono::steady_clock::time_point {};
  const auto clock = [&now] {
    return now;
  };
  const auto allocate = [] {
    return std::make_shared<platf::img_t>();
  };
  auto first = pool.acquire(allocate, clock);
  auto second = pool.acquire(allocate, clock);
  auto third = pool.acquire(allocate, clock);
  std::weak_ptr<platf::img_t> first_reference = first;
  std::weak_ptr<platf::img_t> second_reference = second;
  const auto *third_pointer = third.get();
  first.reset();
  second.reset();
  third.reset();
  now += 2999ms;
  auto current = pool.acquire(allocate, clock);
  ASSERT_EQ(current.get(), third_pointer);
  EXPECT_FALSE(first_reference.expired());
  EXPECT_FALSE(second_reference.expired());
  current.reset();
  now += 1ms;
  current = pool.acquire(allocate, clock);
  EXPECT_EQ(current.get(), third_pointer);
  EXPECT_TRUE(first_reference.expired());
  EXPECT_TRUE(second_reference.expired());
  auto new_image = pool.acquire(allocate, clock);
  EXPECT_NE(new_image, current);
  pool.clear();
  EXPECT_NE(current, nullptr);
  EXPECT_NE(new_image, nullptr);
}

/** @brief An older image still retained by an encoder survives trimming ahead of an idle newer allocation. */
TEST(VideoCapturePoolTest, TrimmingSkipsAnExternallyOwnedLeastRecentImage) {
  video::detail::capture_image_pool_t pool {3};
  auto now = std::chrono::steady_clock::time_point {};
  const auto clock = [&now] {
    return now;
  };
  const auto allocate = [] {
    return std::make_shared<platf::img_t>();
  };
  auto oldest = pool.acquire(allocate, clock);
  auto middle = pool.acquire(allocate, clock);
  auto newest = pool.acquire(allocate, clock);
  std::weak_ptr<platf::img_t> middle_reference = middle;
  middle.reset();
  newest.reset();
  now += 3s;
  auto current = pool.acquire(allocate, clock);
  EXPECT_TRUE(middle_reference.expired());
  EXPECT_NE(current, oldest);
  EXPECT_NE(oldest, nullptr);
}

/** @brief Plane copying preserves black aspect padding for planar and interleaved chroma in both orientations. */
TEST(SoftwareEncoderConversion, PreservesAspectPaddingAcrossSoftwarePixelPlanes) {
  for (const auto format : {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_NV12}) {
    for (const auto &[width, height] : {std::pair {16, 8}, std::pair {8, 16}}) {
      video::avcodec_frame_t frame {av_frame_alloc()};
      ASSERT_NE(frame, nullptr);
      frame->width = 16;
      frame->height = 16;
      frame->format = format;
      frame->color_range = AVCOL_RANGE_MPEG;
      video::avcodec_software_encode_device_t device {2};
      ASSERT_EQ(device.init(width, height, frame.get(), format, false), 0);
      const auto *output = frame.get();
      ASSERT_EQ(device.set_frame(frame.release(), nullptr), 0);
      std::vector<std::uint8_t> pixels(width * height * 4);
      for (std::size_t index = 0; index < pixels.size(); index += 4) {
        pixels[index] = 255;
      }
      platf::img_t image;
      image.data = pixels.data();
      image.width = width;
      image.height = height;
      image.row_pitch = width * 4;
      image.pixel_pitch = 4;
      ASSERT_EQ(device.convert(image), 0);
      EXPECT_EQ(output->data[0][0], 16);
      EXPECT_GT(output->data[0][8 * output->linesize[0] + 8], 16);
      const auto description = av_pix_fmt_desc_get(format);
      for (int plane = 1; plane < av_pix_fmt_count_planes(format); ++plane) {
        const auto center = (8 >> description->log2_chroma_h) * output->linesize[plane] + (8 >> description->log2_chroma_w) * description->comp[plane].step;
        EXPECT_EQ(output->data[plane][0], 128);
        EXPECT_NE(output->data[plane][center], 128);
      }
    }
  }
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

TEST(VideoServiceOwnershipTest, SnapshotChangesDoNotMutateServiceStateAndUnprobedCaptureStopsItsSession) {
  const config::store_t config;
  runtime::infrastructure_t infrastructure;
  display_device::manager_t displays;
  platf::runtime_t platform {
    config.values(),
    infrastructure.mailbox(),
    infrastructure.scheduler(),
    infrastructure.lifetime(),
    displays
#ifdef _WIN32
    ,
    infrastructure.preferences()
#endif
  };
  video::service_t first {config.values(), infrastructure.mailbox(), infrastructure.cursor(), displays, platform};
  video::service_t second {config.values(), std::make_shared<safe::mail_raw_t>(), infrastructure.cursor(), displays, platform};
  auto snapshot = first.capabilities();
  snapshot.active_hevc_mode = 5;
  snapshot.yuv444.fill(true);
  EXPECT_EQ(first.capabilities().active_hevc_mode, 0);
  EXPECT_EQ(second.capabilities().active_hevc_mode, 0);
  EXPECT_FALSE(first.capabilities().yuv444[0]);

  auto session_mail = std::make_shared<safe::mail_raw_t>();
  auto shutdown = session_mail->event<bool>(mail::shutdown);
  first.capture(session_mail, {}, nullptr);
  EXPECT_TRUE(shutdown->peek());
}

TEST(VideoEncoderOwnershipTest, DescriptorCopiesKeepProbeCapabilitiesIndependent) {
  const config::store_t config;
  auto prototype = video::make_software(config.values());
  auto first = prototype;
  auto second = prototype;
  first.h264[video::encoder_t::PASSED] = true;
  first.hevc[video::encoder_t::DYNAMIC_RANGE] = true;
  EXPECT_FALSE(second.h264[video::encoder_t::PASSED]);
  EXPECT_FALSE(second.hevc[video::encoder_t::DYNAMIC_RANGE]);
  EXPECT_FALSE(prototype.h264[video::encoder_t::PASSED]);
  EXPECT_EQ(first.platform_formats, second.platform_formats);
}

/**
 * @brief Factory constructing an encoder descriptor for one immutable host configuration.
 */
using EncoderFactory = video::encoder_t (*)(const config::settings_t &);

struct EncoderTest: PlatformTestSuite, testing::WithParamInterface<EncoderFactory> {
  config::store_t config_;  ///< Immutable encoder fixture configuration.
  runtime::infrastructure_t infrastructure_;  ///< Infrastructure owned by this encoder fixture.
  display_device::manager_t displays_;  ///< Display configuration owner for encoder validation.
  video::service_t videos_ {config_.values(), infrastructure_.mailbox(), infrastructure_.cursor(), displays_, platform()};  ///< Fixture-owned video service.

  void SetUp() override {
    BaseTest::SetUp();
    auto encoder = GetParam()(config_.values());
    if (!videos_.validate_encoder(encoder, false)) {
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
    &video::make_nvenc,
#endif
#ifdef _WIN32
    &video::make_amdvce,
    &video::make_quicksync,
#endif
#if defined(__linux__) || defined(__FreeBSD__)
    &video::make_vaapi,
#endif
#ifdef __APPLE__
    &video::make_videotoolbox,
#endif
    &video::make_software
  ),
  [](const auto &info) {
    const config::store_t config;
    return std::string(info.param(config.values()).name);
  }
);

#if !defined(_WIN32) && !defined(__APPLE__)
TEST(NvencAvcodecOptionsTest, UsesVersionStablePresetAndSpatialAqNames) {
  const config::store_t config;
  const auto encoder = video::make_nvenc(config.values());
  const std::array codecs {
    &encoder.av1,
    &encoder.hevc,
    &encoder.h264,
  };

  for (const auto *codec : codecs) {
    const auto preset = std::ranges::find(codec->common_options, "preset"sv, &video::encoder_t::option_t::name);
    ASSERT_NE(codec->common_options.end(), preset);
    ASSERT_TRUE(std::holds_alternative<const std::string *>(preset->value));
    EXPECT_EQ(&config.values().video.nv_legacy.preset, std::get<const std::string *>(preset->value));

    const auto spatial_aq = std::ranges::find(codec->common_options, "spatial-aq"sv, &video::encoder_t::option_t::name);
    ASSERT_NE(codec->common_options.end(), spatial_aq);
    ASSERT_TRUE(std::holds_alternative<const int *>(spatial_aq->value));
    EXPECT_EQ(&config.values().video.nv_legacy.spatial_aq, std::get<const int *>(spatial_aq->value));

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

#ifdef _WIN32
TEST(AmfH264OptionsTest, CoderUsesConfiguredValue) {
  const config::store_t config;
  const auto encoder = video::make_amdvce(config.values());
  const auto coder_option = std::ranges::find(encoder.h264.common_options, "coder"sv, &video::encoder_t::option_t::name);

  ASSERT_NE(encoder.h264.common_options.end(), coder_option);
  ASSERT_TRUE(std::holds_alternative<const int *>(coder_option->value));
  EXPECT_EQ(&config.values().video.amd.amd_coder, std::get<const int *>(coder_option->value));
}

/**
 * @brief Parameterized coverage for the AMF maximum access-unit-size option mappings.
 */
struct AmfMaxAuSizeOptionsTest: testing::TestWithParam<std::tuple<int, bool>> {};

TEST_P(AmfMaxAuSizeOptionsTest, UsesConfiguredValueForSupportedCodecsOnly) {
  const auto &[index, supported] = GetParam();
  const config::store_t config;
  const auto encoder = video::make_amdvce(config.values());
  const auto *codec = index == 0 ? &encoder.h264 : index == 1 ? &encoder.hevc :
                                                                &encoder.av1;
  const auto option = std::ranges::find(codec->common_options, "max_au_size"sv, &video::encoder_t::option_t::name);

  if (!supported) {
    EXPECT_EQ(codec->common_options.end(), option);
    return;
  }

  ASSERT_NE(codec->common_options.end(), option);
  ASSERT_TRUE(std::holds_alternative<const std::optional<int> *>(option->value));
  EXPECT_EQ(&config.values().video.amd.amd_max_au_size, std::get<const std::optional<int> *>(option->value));
}

INSTANTIATE_TEST_SUITE_P(
  AmfCodecOptions,
  AmfMaxAuSizeOptionsTest,
  testing::Values(
    std::make_tuple(0, true),
    std::make_tuple(1, true),
    std::make_tuple(2, false)
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
    config_.edit_startup().video.amd.amd_max_au_size.reset();
    config_.edit_startup().stream.file_apps = SUNSHINE_SOURCE_DIR "/tests/unit/test_video.cpp";
  }

  config::store_t config_;  ///< Configuration isolated to this parser test.
};

TEST_P(AmfMaxAuSizeConfigTest, AcceptsOnlyFfmpegSupportedRange) {
  const auto &[setting, expected] = GetParam();
  config_.apply_config_for_test(setting);

  EXPECT_EQ(expected, config_.edit_startup().video.amd.amd_max_au_size);
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
TEST(SoftwareEncoderConversion, Bgr0AndNv12) {
  constexpr int w = 320;
  constexpr int h = 240;

  AVFrame *frame = av_frame_alloc();
  ASSERT_NE(frame, nullptr);
  frame->width = w;
  frame->height = h;
  frame->format = AV_PIX_FMT_YUV420P;

  video::avcodec_software_encode_device_t device {2};
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
