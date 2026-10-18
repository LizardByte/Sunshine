/**
 * @file tests/unit/test_video_pyrowave.cpp
 * @brief Regression coverage for PyroWave profiles, chroma and HDR precision, framing, and failures.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <cstring>
#include <limits>

// platform includes
#ifdef __APPLE__
  #include <CoreVideo/CoreVideo.h>
#endif

// lib includes
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#ifdef SUNSHINE_BUILD_VULKAN
  #include <libavutil/hwcontext_vulkan.h>
#endif
}

// local includes
#include <src/config.h>
#include <src/video.h>

namespace video {
  /**
   * @brief Dispatch a converted frame through the common packet queue.
   * @param frame_nr Sequence number.
   * @param session Active encoder.
   * @param packets Destination queue.
   * @param channel_data Owning stream.
   * @param frame_timestamp Capture timestamp.
   * @return Zero on success.
   */
  int encode(int64_t frame_nr, encode_session_t &session, safe::mail_raw_t::queue_t<packet_t> &packets, stream::session_t *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp);

  /**
   * @brief Create an encoder session through the capture pipeline.
   * @param disp Capture display.
   * @param encoder Capture backend descriptor.
   * @param config Negotiated configuration.
   * @param width Captured width.
   * @param height Captured height.
   * @param encode_device Capture conversion device.
   * @return Initialized session, or null on failure.
   */
  std::unique_ptr<encode_session_t> make_encode_session(platf::display_t *disp, const encoder_t &encoder, const config_t &config, int width, int height, std::unique_ptr<platf::encode_device_t> encode_device);

  /**
   * @brief Create a PyroWave capture session with injectable codec entry points.
   * @param encoder Capture backend descriptor.
   * @param config Negotiated configuration.
   * @param width Captured width.
   * @param height Captured height.
   * @param device Capture conversion device.
   * @param api Codec entry points.
   * @return Initialized session, or null on failure.
   */
  std::unique_ptr<encode_session_t> make_pyrowave_encode_session(const encoder_t &encoder, const config_t &config, int width, int height, std::unique_ptr<platf::avcodec_encode_device_t> device, const pyrowave::api_t &api);

  /**
   * @brief Request a capture converter for the negotiated format.
   * @param disp Capture display.
   * @param encoder Capture backend descriptor.
   * @param config Negotiated configuration.
   * @return Capture device, or null when unsupported.
   */
  std::unique_ptr<platf::encode_device_t> make_encode_device(platf::display_t &disp, const encoder_t &encoder, const config_t &config);

  /**
   * @brief Refresh available PyroWave profiles.
   * @param encoder Capture backend descriptor.
   */
  void probe_pyrowave(const encoder_t &encoder);
}  // namespace video

namespace nvhttp {
  /**
   * @brief Obtain the same capability mask used in server-info responses.
   * @return Probed codec support bits.
   */
  uint32_t get_codec_mode_flags();
}  // namespace nvhttp

TEST(PyrowaveConfigTest, ServerInfoReportsEachProfileIndependently) {
  const auto previous = video::active_pyrowave_profiles;
  const auto restore = util::fail_guard([previous]() {
    video::active_pyrowave_profiles = previous;
  });
  video::active_pyrowave_profiles = 0;
  const auto baseline = nvhttp::get_codec_mode_flags();
  EXPECT_EQ(baseline & video::pyrowave::scm_mask, 0);
  for (uint32_t profiles = 0; profiles < 16; ++profiles) {
    video::active_pyrowave_profiles = profiles << 23;
    EXPECT_EQ(nvhttp::get_codec_mode_flags(), baseline | (profiles << 23));
  }
  video::active_pyrowave_profiles = ~video::pyrowave::scm_mask;
  EXPECT_EQ(nvhttp::get_codec_mode_flags(), baseline);
}

namespace {
  /**
   * @brief Return a small, valid PyroWave configuration for deterministic tests.
   * @return 64x48 SDR 4:2:0 at 60 FPS.
   */
  video::config_t pyrowave_config() {
    return {64, 48, 60, 6000, 200000, 1, 0, 3, video::pyrowave::video_format, 0, 0, 0};
  }

  /**
   * @brief Record converter requests without requiring a display or video encoder.
   */
  class pyrowave_test_display_t: public platf::display_t {
  public:
    /**
     * @copydoc platf::display_t::capture
     */
    platf::capture_e capture(const push_captured_image_cb_t &, const pull_free_image_cb_t &, bool *) override {
      return platf::capture_e::ok;
    }

    /**
     * @copydoc platf::display_t::alloc_img
     */
    std::shared_ptr<platf::img_t> alloc_img() override {
      return {};
    }

    /**
     * @copydoc platf::display_t::dummy_img
     */
    int dummy_img(platf::img_t *) override {
      return -1;
    }

    /**
     * @copydoc platf::display_t::make_avcodec_encode_device
     */
    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e format) override {
      requested = format;
      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    platf::pix_fmt_e requested = platf::pix_fmt_e::unknown;  ///< Last requested conversion format.
  };
}  // namespace

TEST(PyrowaveConfigTest, NegotiatesOnlySuccessfullyProbedProfiles) {
  auto config = pyrowave_config();
  const uint32_t flags[] {video::pyrowave::scm_sdr, video::pyrowave::scm_sdr_444, video::pyrowave::scm_hdr, video::pyrowave::scm_hdr_444};
  for (uint32_t profiles = 0; profiles < 16; ++profiles) {
    std::array<int, 4> calls {};
    const auto probed = video::pyrowave::probe_profiles([&](const video::config_t &probe) {
      EXPECT_TRUE(video::pyrowave::valid_config(probe));
      const auto profile = probe.dynamicRange * 2 + probe.chromaSamplingType;
      ++calls.at(profile);
      return (profiles & (1 << profile)) != 0;
    });
    EXPECT_EQ(probed, profiles << 23);
    EXPECT_EQ(calls, (std::array {1, 1, 1, 1}));
    for (int profile = 0; profile < 4; ++profile) {
      config.chromaSamplingType = profile % 2;
      config.dynamicRange = profile / 2;
      EXPECT_EQ(video::pyrowave::supported(config, probed), (profiles & (1 << profile)) != 0);
      EXPECT_TRUE(video::pyrowave::supported(config, flags[profile]));
    }
  }
}

TEST(PyrowaveConfigTest, RejectsMalformedOrUnsupportedRequests) {
  const auto valid = pyrowave_config();
  EXPECT_TRUE(video::pyrowave::valid_config(valid));
  const std::vector<std::pair<int video::config_t::*, int>> invalid {
    {&video::config_t::videoFormat, -1},
    {&video::config_t::videoFormat, 2},
    {&video::config_t::width, 0},
    {&video::config_t::width, -2},
    {&video::config_t::width, 63},
    {&video::config_t::width, 16386},
    {&video::config_t::height, 0},
    {&video::config_t::height, -2},
    {&video::config_t::height, 47},
    {&video::config_t::height, 16386},
    {&video::config_t::dynamicRange, -1},
    {&video::config_t::dynamicRange, 2},
    {&video::config_t::chromaSamplingType, -1},
    {&video::config_t::chromaSamplingType, 2},
    {&video::config_t::bitrate, 0},
    {&video::config_t::bitrate, -1},
    {&video::config_t::bitrate, std::numeric_limits<int>::max()},
    {&video::config_t::framerate, 0},
    {&video::config_t::framerate, -1},
    {&video::config_t::framerateX100, -1},
  };
  for (const auto &[member, value] : invalid) {
    auto config = valid;
    config.*member = value;
    EXPECT_FALSE(video::pyrowave::valid_config(config)) << value;
    EXPECT_FALSE(video::pyrowave::supported(config, video::pyrowave::scm_mask)) << value;
  }
  auto config = valid;
  config.chromaSamplingType = 1;
  config.width = 63;
  config.height = 47;
  EXPECT_TRUE(video::pyrowave::valid_config(config));
}

/**
 * @brief Select capture layouts independently of the hardware video encoder's codec support.
 */
TEST(PyrowaveConfigTest, SelectsCaptureFormatsForEachProfile) {
  video::encoder_t native {
    .platform_formats = std::make_unique<video::encoder_platform_formats_nvenc>(
      platf::mem_type_e::dxgi,
      platf::pix_fmt_e::nv12,
      platf::pix_fmt_e::p010,
      platf::pix_fmt_e::ayuv,
      platf::pix_fmt_e::yuv444p16
    )
  };
  video::encoder_platform_formats_avcodec cuda {
    AV_HWDEVICE_TYPE_CUDA,
    AV_HWDEVICE_TYPE_NONE,
    AV_PIX_FMT_CUDA,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P16,
    {}
  };
  const AVPixelFormat software_formats[] {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV444P10};
  const AVPixelFormat dxgi_formats[] {AV_PIX_FMT_NV12, AV_PIX_FMT_VUYX, AV_PIX_FMT_P010, AV_PIX_FMT_XV30};
  const AVPixelFormat cuda_formats[] {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV444P, AV_PIX_FMT_P010, AV_PIX_FMT_YUV444P16};
  pyrowave_test_display_t display;
  auto config = pyrowave_config();
  for (int profile = 0; profile < 4; ++profile) {
    config.chromaSamplingType = profile % 2;
    config.dynamicRange = profile / 2;
    EXPECT_EQ(video::pyrowave::capture_format(*video::software.platform_formats, config), software_formats[profile]);
    EXPECT_EQ(video::pyrowave::capture_format(*native.platform_formats, config), dxgi_formats[profile]);
    EXPECT_EQ(video::pyrowave::capture_format(cuda, config), cuda_formats[profile]);
    ASSERT_NE(video::make_encode_device(display, native, config), nullptr);
    EXPECT_EQ(display.requested, video::map_pix_fmt(dxgi_formats[profile]));
  }
  video::encoder_platform_formats_t unknown {};
  unknown.dev_type = platf::mem_type_e::system;
  EXPECT_EQ(video::pyrowave::capture_format(unknown, config), AV_PIX_FMT_NONE);
  cuda.avcodec_pix_fmt_yuv444_8bit = AV_PIX_FMT_NONE;
  cuda.avcodec_pix_fmt_yuv444_10bit = AV_PIX_FMT_NONE;
  EXPECT_EQ(video::pyrowave::capture_format(cuda, config), AV_PIX_FMT_NONE);
  video::encoder_t unavailable {
    .platform_formats = std::make_unique<video::encoder_platform_formats_avcodec>(cuda)
  };
  display.requested = platf::pix_fmt_e::unknown;
  EXPECT_EQ(video::make_encode_device(display, unavailable, config), nullptr);
  EXPECT_EQ(display.requested, platf::pix_fmt_e::unknown);
  config.dynamicRange = 0;
  EXPECT_EQ(video::make_encode_session(nullptr, unavailable, config, 64, 48, std::make_unique<platf::avcodec_encode_device_t>()), nullptr);
}

/**
 * @brief Respect host bitrate limits and fractional rates without integer overflow.
 */
TEST(PyrowaveConfigTest, BudgetsCappedAndFractionalRatesWithoutIntegerOverflow) {
  const auto previous = config::video.max_bitrate;
  const auto restore = util::fail_guard([previous]() {
    config::video.max_bitrate = previous;
  });
  config::video.max_bitrate = 0;
  auto config = pyrowave_config();
  EXPECT_EQ(video::pyrowave::frame_budget(config), 416666);
  config.framerateX100 = 5994;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 417083);
  config.framerateX100 = 0;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 416666);
  config.bitrate = 1;
  config.framerate = 1000;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 0);
  config.bitrate = std::numeric_limits<int>::max();
  config.framerate = std::numeric_limits<int>::max();
  EXPECT_EQ(video::pyrowave::frame_budget(config), 125);

  config = pyrowave_config();
  config::video.max_bitrate = 5000;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 10416);
  config.framerateX100 = 5994;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 10427);
  config.framerateX100 = 0;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 10416);
  config.bitrate = 2000;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 4166);
  config.bitrate = 5000;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 10416);
  config = pyrowave_config();
  config::video.max_bitrate = -1;
  EXPECT_EQ(video::pyrowave::frame_budget(config), 416666);
}

TEST(PyrowaveConfigTest, PreservesHdrWithoutConsultingUnrelatedCodecCapabilities) {
  auto config = pyrowave_config();
  config.dynamicRange = 1;
  video::encoder_t encoder {};
  for (int chroma : {0, 1}) {
    config.chromaSamplingType = chroma;
    EXPECT_EQ(video::resolve_dynamic_range(encoder, config).dynamicRange, 1);
  }
}

TEST(PyrowaveConfigTest, DisablingTheCodecClearsPreviouslyProbedProfiles) {
  const auto previous_profiles = video::active_pyrowave_profiles;
  const auto previous_enabled = config::video.pyrowave;
  const auto restore = util::fail_guard([&]() {
    video::active_pyrowave_profiles = previous_profiles;
    config::video.pyrowave = previous_enabled;
  });
  video::active_pyrowave_profiles = video::pyrowave::scm_mask;
  config::video.pyrowave = false;
  video::probe_pyrowave({});
  EXPECT_EQ(video::active_pyrowave_profiles, 0);
}

TEST(PyrowaveConfigTest, RejectsInvalidCaptureDevicesAndHdrFromRam) {
  auto config = pyrowave_config();
  EXPECT_EQ(video::make_encode_session(nullptr, video::software, config, 64, 48, nullptr), nullptr);
  EXPECT_EQ(video::make_encode_session(nullptr, {}, config, 64, 48, std::make_unique<platf::avcodec_encode_device_t>()), nullptr);
  EXPECT_EQ(video::make_encode_session(nullptr, video::software, config, 0, 48, std::make_unique<platf::avcodec_encode_device_t>()), nullptr);
  EXPECT_EQ(video::make_encode_session(nullptr, video::software, config, 64, 0, std::make_unique<platf::avcodec_encode_device_t>()), nullptr);
  config.width = 63;
  EXPECT_EQ(video::make_encode_session(nullptr, video::software, config, 64, 48, std::make_unique<platf::avcodec_encode_device_t>()), nullptr);
  config.width = 64;
  config.dynamicRange = 1;
  EXPECT_EQ(video::make_encode_session(nullptr, video::software, config, 64, 48, std::make_unique<platf::avcodec_encode_device_t>()), nullptr);
}

namespace {
  /**
   * @brief Converter supplying padded planar or packed YUV frames without display hardware.
   */
  class pyrowave_test_converter_t: public platf::avcodec_encode_device_t {
  public:
    /**
     * @brief Fill alternating luma and checkerboard chroma with full-range endpoint values.
     * @param format Pixel storage layout supplied by the capture converter.
     * @param negative_stride Store rows in reverse order within each plane.
     */
    explicit pyrowave_test_converter_t(AVPixelFormat format, bool negative_stride = false) {
      owned.reset(av_frame_alloc());
      frame = owned.get();
      frame->format = format;
      frame->width = 64;
      frame->height = 48;
      colorspace = {video::colorspace_e::bt2020, true, 10};
      frame->colorspace = AVCOL_SPC_BT2020_NCL;
      frame->color_primaries = AVCOL_PRI_BT2020;
      frame->color_trc = AVCOL_TRC_SMPTE2084;
      frame->color_range = AVCOL_RANGE_JPEG;
      if (av_frame_get_buffer(frame, 128) < 0) {
        throw std::bad_alloc();
      }
      for (auto buffer : frame->buf) {
        if (buffer) {
          std::memset(buffer->data, 0, buffer->size);
        }
      }
      const auto desc = av_pix_fmt_desc_get(format);
      if (negative_stride) {
        for (int plane = 0; plane < av_pix_fmt_count_planes(format); ++plane) {
          const auto height = 48 >> (plane ? desc->log2_chroma_h : 0);
          frame->data[plane] += (height - 1) * frame->linesize[plane];
          frame->linesize[plane] *= -1;
        }
      }
      const auto maximum = desc->comp[0].depth > 8 ? 1023 : 255;
      for (int component = 0; component < 3; ++component) {
        const auto width = 64 >> (component ? desc->log2_chroma_w : 0);
        const auto height = 48 >> (component ? desc->log2_chroma_h : 0);
        std::array<uint16_t, 64> row {};
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            const auto value = (x + (component ? y : 0) + (component == 2)) % 2 ? maximum : 1;
            row[x] = uint16_t(value << (desc->comp[0].depth == 16 ? 6 : 0));
          }
          av_write_image_line(row.data(), frame->data, frame->linesize, desc, 0, y, component, width);
        }
      }
    }

    /**
     * @brief Observe destruction before releasing the converter's frame.
     */
    ~pyrowave_test_converter_t() override {
      if (on_destroy) {
        on_destroy();
      }
    }

    /**
     * @brief Supply the prepared pixels or simulate a conversion error.
     * @param img Unused captured image.
     * @return Configured conversion status.
     */
    int convert(platf::img_t &img) override {
      return status;
    }

    int status = 0;  ///< Simulated conversion result.
    video::avcodec_frame_t owned;  ///< Owner of the input frame.
    std::function<void()> on_destroy;  ///< Optional lifetime assertion.
  };

  /**
   * @brief Mock codec state with explicit control over each failure boundary.
   */
  struct pyrowave_api_state_t {
    uint32_t major = 1;  ///< Reported ABI major version.
    pyrowave_result device_result = PYROWAVE_SUCCESS;  ///< Device creation status.
    pyrowave_result encoder_result = PYROWAVE_SUCCESS;  ///< Encoder creation status.
    pyrowave_result encode_result = PYROWAVE_SUCCESS;  ///< Encode status.
    pyrowave_result count_result = PYROWAVE_SUCCESS;  ///< Packet count status.
    pyrowave_result packet_result = PYROWAVE_SUCCESS;  ///< Packetization status.
    size_t count = 2;  ///< Packet allocation requested from the caller.
    size_t output_count = 2;  ///< Packet count actually written.
    size_t packet_offset = 2;  ///< First packet offset in the backing buffer.
    size_t packet_size = 3;  ///< First packet length.
    int encoded = 0;  ///< Number of codec calls, including repeated frames.
    size_t budget = 0;  ///< Last requested bitrate budget.
    pyrowave_cpu_buffer input {};  ///< Last codec input layout and borrowed plane addresses.
    pyrowave_chroma_subsampling chroma {};  ///< Chroma sampling used to create the encoder.
    std::optional<std::array<uint16_t, 3>> solid_color;  ///< Expected uniform YUV values instead of the checkerboard.
    std::array<uint16_t, 4> samples {};  ///< First two luma samples and first Cb/Cr samples.
    std::vector<std::string> destroyed;  ///< Resource destruction order.
  };

  thread_local pyrowave_api_state_t *api_state;  ///< State for the current synchronous fake API test.

  /**
   * @brief Build deterministic C API callbacks without a Vulkan driver.
   * @return Injectable C API table.
   */
  video::pyrowave::api_t fake_pyrowave_api() {
    video::pyrowave::api_t api;
    api.get_api_version = [](uint32_t *major, uint32_t *minor, uint32_t *patch) {
      *major = api_state->major;
      *minor = PYROWAVE_API_VERSION_MINOR;
      *patch = PYROWAVE_API_VERSION_PATCH;
    };
    api.create_device = [](pyrowave_device *device) {
      if (api_state->device_result == PYROWAVE_SUCCESS) {
        *device = reinterpret_cast<pyrowave_device>(api_state);
      }
      return api_state->device_result;
    };
    api.destroy_device = [](pyrowave_device) {
      api_state->destroyed.emplace_back("device");
    };
    api.create_encoder = [](const pyrowave_encoder_create_info *info, pyrowave_encoder *encoder) {
      EXPECT_EQ(info->width, 64);
      EXPECT_EQ(info->height, 48);
      api_state->chroma = info->chroma;
      if (api_state->encoder_result == PYROWAVE_SUCCESS) {
        *encoder = reinterpret_cast<pyrowave_encoder>(api_state);
      }
      return api_state->encoder_result;
    };
    api.destroy_encoder = [](pyrowave_encoder) {
      api_state->destroyed.emplace_back("encoder");
    };
    api.encode_cpu = [](pyrowave_encoder, const pyrowave_cpu_buffer *input, const pyrowave_rate_control *rate) {
      ++api_state->encoded;
      api_state->budget = rate->maximum_bitstream_size;
      api_state->input = *input;
      EXPECT_EQ(input->width, 64);
      EXPECT_EQ(input->height, 48);
      const bool hdr = input->format == PYROWAVE_CPU_BUFFER_FORMAT_YUV420P10 || input->format == PYROWAVE_CPU_BUFFER_FORMAT_YUV444P10;
      const bool nv12 = input->format == PYROWAVE_CPU_BUFFER_FORMAT_NV12;
      const bool subsampled = api_state->chroma == PYROWAVE_CHROMA_SUBSAMPLING_420;
      for (int component = 0; component < 3; ++component) {
        const auto plane = nv12 && component ? 1 : component;
        const auto width = component && subsampled ? 32 : 64;
        const auto height = component && subsampled ? 24 : 48;
        EXPECT_GE(input->row_stride_in_bytes[plane], width * (hdr || (nv12 && component) ? 2 : 1));
        EXPECT_EQ(input->plane_size_in_bytes[plane], input->row_stride_in_bytes[plane] * height);
        for (int y = 0; y < height; ++y) {
          const auto row = static_cast<uint8_t *>(input->data[plane]) + y * input->row_stride_in_bytes[plane];
          for (int x = 0; x < width; ++x) {
            const auto value = hdr ? reinterpret_cast<const uint16_t *>(row)[x] : row[nv12 && component ? 2 * x + (component == 2) : x];
            const auto expected = api_state->solid_color ? (*api_state->solid_color)[component] :
                                                           ((x + (component ? y : 0) + (component == 2)) % 2 ? (hdr ? 1023 : 255) : 1);
            EXPECT_EQ(value, expected) << "plane " << plane << ", x " << x << ", y " << y;
          }
        }
      }
      if (nv12) {
        EXPECT_EQ(input->data[2], nullptr);
        EXPECT_EQ(input->row_stride_in_bytes[2], 0);
        EXPECT_EQ(input->plane_size_in_bytes[2], 0);
        api_state->samples = {static_cast<uint8_t *>(input->data[0])[0], static_cast<uint8_t *>(input->data[0])[1], static_cast<uint8_t *>(input->data[1])[0], static_cast<uint8_t *>(input->data[1])[1]};
      } else if (hdr) {
        api_state->samples = {static_cast<uint16_t *>(input->data[0])[0], static_cast<uint16_t *>(input->data[0])[1], static_cast<uint16_t *>(input->data[1])[0], static_cast<uint16_t *>(input->data[2])[0]};
      } else {
        api_state->samples = {static_cast<uint8_t *>(input->data[0])[0], static_cast<uint8_t *>(input->data[0])[1], static_cast<uint8_t *>(input->data[1])[0], static_cast<uint8_t *>(input->data[2])[0]};
      }
      return api_state->encode_result;
    };
    api.num_packets = [](pyrowave_encoder, size_t boundary, size_t *count) {
      EXPECT_EQ(boundary, video::pyrowave::packet_boundary);
      *count = api_state->count;
      return api_state->count_result;
    };
    api.packetize = [](pyrowave_encoder, pyrowave_packet *packets, size_t, size_t *count, void *data, size_t size) {
      EXPECT_GE(size, 12);
      packets[0] = {api_state->packet_offset, api_state->packet_size};
      if (api_state->count > 1) {
        packets[1] = {9, 2};
      }
      const uint8_t bytes[] {0, 0, 0x11, 0x22, 0x33, 0, 0, 0, 0, 0x44, 0x55, 0};
      std::memcpy(data, bytes, sizeof(bytes));
      *count = api_state->output_count;
      return api_state->packet_result;
    };
    return api;
  }
}  // namespace

/**
 * @brief Reset fake C API state between independent encoder tests.
 */
class PyrowaveSessionTest: public testing::Test {
protected:
  /**
   * @brief Bind this test's state to the fake C callbacks.
   */
  void SetUp() override {
    api_state = &state;
  }

  /**
   * @brief Clear the callback state after each test.
   */
  void TearDown() override {
    api_state = nullptr;
  }

  pyrowave_api_state_t state;  ///< Per-test codec status and observations.
};

TEST_F(PyrowaveSessionTest, PreservesFrameTimingAndStreamOwnershipInThePacketQueue) {
  auto session = video::pyrowave::make_session(pyrowave_config(), std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12), fake_pyrowave_api());
  ASSERT_NE(session, nullptr);
  auto mailbox = std::make_shared<safe::mail_raw_t>();
  auto packets = mailbox->queue<video::packet_t>("pyrowave-test");
  auto stream = reinterpret_cast<stream::session_t *>(&state);
  const auto timestamp = std::chrono::steady_clock::now();
  EXPECT_EQ(video::encode(1, *session, packets, stream, timestamp), -1);
  EXPECT_FALSE(packets->peek());
  platf::img_t image {};
  ASSERT_EQ(session->convert(image), 0);
  ASSERT_EQ(video::encode(42, *session, packets, stream, timestamp), 0);
  ASSERT_TRUE(packets->peek());
  auto packet = packets->pop();
  EXPECT_EQ(packet->frame_index(), 42);
  EXPECT_EQ(packet->channel_data, stream);
  EXPECT_EQ(packet->frame_timestamp, timestamp);
  ASSERT_EQ(video::encode(43, *session, packets, nullptr, {}), 0);
  ASSERT_TRUE(packets->peek());
  packet = packets->pop();
  EXPECT_FALSE(packet->frame_timestamp.has_value());
  EXPECT_EQ(packet->channel_data, nullptr);
}

/**
 * @brief Attach missing VideoToolbox pool references while preserving existing references and lifetimes.
 */
TEST_F(PyrowaveSessionTest, RetainsTheBorrowedHardwarePoolUntilAfterConverterDestruction) {
  const std::pair<AVPixelFormat, bool> cases[] {
    {AV_PIX_FMT_NV12, false},
    {AV_PIX_FMT_VIDEOTOOLBOX, false},
    {AV_PIX_FMT_VIDEOTOOLBOX, true},
  };
  for (const auto &[format, has_pool] : cases) {
    bool released = false;
    AVHWFramesContext pool {};
    pool.sw_format = AV_PIX_FMT_NV12;
    video::avcodec_buffer_t frames {av_buffer_create(reinterpret_cast<uint8_t *>(&pool), sizeof(pool), [](void *opaque, uint8_t *) {
      *static_cast<bool *>(opaque) = true;
    },
                                                     &released,
                                                     0)};
    ASSERT_NE(frames, nullptr);
    auto converter = std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12);
    const auto frame = converter->frame;
    frame->format = format;
    if (has_pool) {
      frame->hw_frames_ctx = av_buffer_ref(frames.get());
      ASSERT_NE(frame->hw_frames_ctx, nullptr);
    }
    const auto original_pool = frame->hw_frames_ctx;
    const auto pool_buffer = frames->buffer;
    converter->on_destroy = [&]() {
      EXPECT_FALSE(released);
    };
    auto session = video::pyrowave::make_session(pyrowave_config(), std::move(converter), fake_pyrowave_api(), std::move(frames));
    ASSERT_NE(session, nullptr);
    EXPECT_FALSE(released);
    if (format == AV_PIX_FMT_VIDEOTOOLBOX) {
      ASSERT_NE(frame->hw_frames_ctx, nullptr);
      EXPECT_EQ(frame->hw_frames_ctx->buffer, pool_buffer);
      EXPECT_EQ(av_buffer_get_ref_count(frame->hw_frames_ctx), 2);
      if (has_pool) {
        EXPECT_EQ(frame->hw_frames_ctx, original_pool);
      }
    } else {
      EXPECT_EQ(frame->hw_frames_ctx, nullptr);
      platf::img_t image {};
      EXPECT_EQ(session->convert(image), 0);
    }
    EXPECT_FALSE(released);
    session.reset();
    EXPECT_TRUE(released);
  }
}

/**
 * @brief Reuse compatible planes, retain repeated frames, and convert other layouts without losing samples.
 */
TEST_F(PyrowaveSessionTest, EncodesSdrAndHdrWithExactPlaneValuesAndFraming) {
  const AVPixelFormat formats[] {AV_PIX_FMT_NV12, AV_PIX_FMT_P010, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P16, AV_PIX_FMT_VUYX, AV_PIX_FMT_XV30};
  for (auto format : formats) {
    for (int storage = 0; storage < 3; ++storage) {
      SCOPED_TRACE(av_get_pix_fmt_name(format));
      SCOPED_TRACE(storage);
      const auto desc = av_pix_fmt_desc_get(format);
      const bool hdr = desc->comp[0].depth > 8;
      const bool yuv444 = desc->log2_chroma_w == 0;
      auto config = pyrowave_config();
      config.dynamicRange = hdr;
      config.chromaSamplingType = yuv444;
      auto converter = std::make_unique<pyrowave_test_converter_t>(format, storage == 1);
      const auto raw = converter.get();
      video::avcodec_frame_t owner;
      if (storage == 2) {
        owner.reset(av_frame_clone(raw->frame));
        ASSERT_NE(owner, nullptr);
        for (auto &buffer : raw->frame->buf) {
          av_buffer_unref(&buffer);
        }
      }
      auto session = video::pyrowave::make_session(config, std::move(converter), fake_pyrowave_api());
      ASSERT_NE(session, nullptr);
      EXPECT_EQ(session->encode(1), nullptr);
      platf::img_t image {};
      ASSERT_EQ(session->convert(image), 0);
      session->request_idr_frame();
      session->request_normal_frame();
      session->invalidate_ref_frames(1, 2);
      auto packet = session->encode(42);
      ASSERT_NE(packet, nullptr);
      EXPECT_TRUE(packet->is_idr());
      EXPECT_EQ(packet->frame_index(), 42);
      const std::vector<uint8_t> expected {'P', 'Y', 'R', 'W', 1, 0, 2, 0, 0, 0, 0, 3, 0x11, 0x22, 0x33, 0, 0, 0, 2, 0x44, 0x55};
      EXPECT_EQ(std::vector(packet->data(), packet->data() + packet->data_size()), expected);
      EXPECT_EQ(state.budget, 416666);
      EXPECT_EQ(state.chroma, yuv444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420);
      EXPECT_EQ(state.input.format, format == AV_PIX_FMT_NV12 ? PYROWAVE_CPU_BUFFER_FORMAT_NV12 : yuv444 ? (hdr ? PYROWAVE_CPU_BUFFER_FORMAT_YUV444P10 : PYROWAVE_CPU_BUFFER_FORMAT_YUV444P) :
                                                                                                           (hdr ? PYROWAVE_CPU_BUFFER_FORMAT_YUV420P10 : PYROWAVE_CPU_BUFFER_FORMAT_YUV420P));
      const bool direct = storage == 0 && (format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUV420P10 || format == AV_PIX_FMT_YUV444P || format == AV_PIX_FMT_YUV444P10);
      if (direct) {
        for (int plane = 0; plane < av_pix_fmt_count_planes(format); ++plane) {
          EXPECT_EQ(state.input.data[plane], raw->frame->data[plane]);
          EXPECT_EQ(state.input.row_stride_in_bytes[plane], raw->frame->linesize[plane]);
        }
        EXPECT_EQ(av_buffer_get_ref_count(raw->frame->buf[0]), 2);
      } else {
        EXPECT_NE(state.input.data[0], raw->frame->data[0]);
      }
      EXPECT_EQ(state.samples, (std::array<uint16_t, 4> {1, uint16_t(hdr ? 1023 : 255), 1, uint16_t(hdr ? 1023 : 255)}));
      // Repeated capture content must be encoded again so the codec sequence advances.
      ASSERT_EQ(session->convert(image), 0);
      raw->owned.reset();
      owner.reset();
      EXPECT_NE(session->encode(43), nullptr);
      session.reset();
      EXPECT_EQ(state.destroyed, (std::vector<std::string> {"encoder", "device"}));
      state.destroyed.clear();
    }
  }
  EXPECT_EQ(state.encoded, 6 * std::size(formats));
}

/**
 * @brief Keep the codec's cached upload layout stable when a frame needs scaling.
 */
TEST_F(PyrowaveSessionTest, KeepsTheInputLayoutWhenScalingFrames) {
  state.solid_color = std::array<uint16_t, 3> {128, 128, 128};
  for (auto format : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P}) {
    SCOPED_TRACE(av_get_pix_fmt_name(format));
    auto converter = std::make_unique<pyrowave_test_converter_t>(format);
    const auto frame = converter->frame;
    std::memset(frame->buf[0]->data, 128, frame->buf[0]->size);
    auto config = pyrowave_config();
    config.chromaSamplingType = format == AV_PIX_FMT_YUV444P;
    auto session = video::pyrowave::make_session(config, std::move(converter), fake_pyrowave_api());
    ASSERT_NE(session, nullptr);
    platf::img_t image {};
    for (int scale : {1, 2, 1}) {
      frame->width = 64 / scale;
      frame->height = 48 / scale;
      ASSERT_EQ(session->convert(image), 0);
      ASSERT_NE(session->encode(scale), nullptr);
      EXPECT_EQ(state.input.format, format == AV_PIX_FMT_NV12 ? PYROWAVE_CPU_BUFFER_FORMAT_NV12 : format == AV_PIX_FMT_YUV420P ? PYROWAVE_CPU_BUFFER_FORMAT_YUV420P :
                                                                                                                                 PYROWAVE_CPU_BUFFER_FORMAT_YUV444P);
      EXPECT_EQ(state.input.data[0] == frame->data[0], scale == 1);
    }
  }
}

/**
 * @brief Preserve SDR/HDR samples when reading Vulkan frames or borrowed macOS capture buffers.
 */
TEST_F(PyrowaveSessionTest, DownloadsHardwareFramesWithoutLosingTenBitPrecision) {
#ifdef __APPLE__
  constexpr auto device_type = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
  constexpr auto hardware_format = AV_PIX_FMT_VIDEOTOOLBOX;
  const AVPixelFormat formats[] {AV_PIX_FMT_NV12, AV_PIX_FMT_P010};
#else
  constexpr auto device_type = AV_HWDEVICE_TYPE_VULKAN;
  constexpr auto hardware_format = AV_PIX_FMT_VULKAN;
  const AVPixelFormat formats[] {AV_PIX_FMT_NV12, AV_PIX_FMT_P010, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P16};
#endif
  AVBufferRef *raw_device = nullptr;
  if (av_hwdevice_ctx_create(&raw_device, device_type, nullptr, nullptr, 0) < 0) {
    GTEST_SKIP() << "An FFmpeg-compatible " << av_hwdevice_get_type_name(device_type) << " device is unavailable";
  }
  video::avcodec_buffer_t device {raw_device};
  for (auto format : formats) {
    SCOPED_TRACE(av_get_pix_fmt_name(format));
    const auto desc = av_pix_fmt_desc_get(format);
    const bool hdr = desc->comp[0].depth > 8;
    auto config = pyrowave_config();
    config.dynamicRange = hdr;
    config.chromaSamplingType = desc->log2_chroma_w == 0;
    auto converter = std::make_unique<pyrowave_test_converter_t>(format);
    auto host = std::move(converter->owned);
    converter->owned.reset(av_frame_alloc());
    converter->frame = converter->owned.get();
    ASSERT_NE(converter->frame, nullptr);
    video::avcodec_buffer_t frames {av_hwframe_ctx_alloc(device.get())};
    ASSERT_NE(frames, nullptr);
    auto pool = reinterpret_cast<AVHWFramesContext *>(frames->data);
    pool->format = hardware_format;
    pool->sw_format = format;
    pool->width = 64;
    pool->height = 48;
    ASSERT_EQ(av_hwframe_ctx_init(frames.get()), 0);
#ifdef __APPLE__
    CVPixelBufferRef pixel_buffer = nullptr;
    ASSERT_EQ(CVPixelBufferCreate(kCFAllocatorDefault, 64, 48, hdr ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, nullptr, &pixel_buffer), kCVReturnSuccess);
    auto release_buffer = util::fail_guard([&]() {
      CVPixelBufferRelease(pixel_buffer);
    });
    converter->frame->buf[0] = av_buffer_create(reinterpret_cast<uint8_t *>(pixel_buffer), 0, [](void *, uint8_t *data) {
      CVPixelBufferRelease(reinterpret_cast<CVPixelBufferRef>(data));
    },
                                                nullptr,
                                                0);
    ASSERT_NE(converter->frame->buf[0], nullptr);
    release_buffer.disable();
    converter->frame->data[3] = reinterpret_cast<uint8_t *>(pixel_buffer);
    converter->frame->format = hardware_format;
    converter->frame->width = 64;
    converter->frame->height = 48;
    converter->frame->hw_frames_ctx = av_buffer_ref(frames.get());
    ASSERT_NE(converter->frame->hw_frames_ctx, nullptr);
#else
    ASSERT_EQ(av_hwframe_get_buffer(frames.get(), converter->frame, 0), 0);
#endif
    ASSERT_EQ(av_hwframe_transfer_data(converter->frame, host.get(), 0), 0);
    ASSERT_EQ(av_frame_copy_props(converter->frame, host.get()), 0);
#ifdef __APPLE__
    // Captured CVPixelBuffers arrive without an FFmpeg frame-pool reference.
    av_buffer_unref(&converter->frame->hw_frames_ctx);
#endif
    auto session = video::pyrowave::make_session(config, std::move(converter), fake_pyrowave_api(), std::move(frames));
    ASSERT_NE(session, nullptr);
    platf::img_t image {};
    for (int frame = 1; frame <= 2; ++frame) {
      ASSERT_EQ(session->convert(image), 0);
      ASSERT_NE(session->encode(frame), nullptr);
      EXPECT_EQ(state.samples, (std::array<uint16_t, 4> {1, uint16_t(hdr ? 1023 : 255), 1, uint16_t(hdr ? 1023 : 255)}));
    }
    pool->sw_format = AV_PIX_FMT_NONE;
    EXPECT_EQ(session->convert(image), -1);
    EXPECT_EQ(session->encode(3), nullptr);
    pool->sw_format = format;
  }
}

TEST_F(PyrowaveSessionTest, ClearsReadinessAfterConversionFailureAndRejectsWrongDepthOrChroma) {
  auto converter = std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12);
  auto raw = converter.get();
  auto session = video::pyrowave::make_session(pyrowave_config(), std::move(converter), fake_pyrowave_api());
  ASSERT_NE(session, nullptr);
  platf::img_t image {};
  ASSERT_EQ(session->convert(image), 0);
  raw->status = -1;
  EXPECT_EQ(session->convert(image), -1);
  EXPECT_EQ(session->encode(1), nullptr);
  raw->status = 0;
  raw->frame->format = AV_PIX_FMT_RGB24;
  EXPECT_EQ(session->convert(image), -1);
  raw->frame->format = AV_PIX_FMT_P010;
  EXPECT_EQ(session->convert(image), -1);
  raw->frame->format = AV_PIX_FMT_YUV444P;
  EXPECT_EQ(session->convert(image), -1);
  raw->frame->format = AV_PIX_FMT_NV12;
  raw->frame->width = 0;
  EXPECT_EQ(session->convert(image), -1);
  EXPECT_EQ(session->encode(1), nullptr);
  raw->frame->width = 64;
  const auto restore_allocations = util::fail_guard([]() {
    av_max_alloc(std::numeric_limits<int>::max());
  });
  av_max_alloc(0);
  EXPECT_EQ(session->convert(image), -1);
  EXPECT_EQ(session->encode(1), nullptr);
  EXPECT_EQ(state.encoded, 0);
}

/**
 * @brief Reject invalid frames and missing or unretainable VideoToolbox pools before creating a codec.
 */
TEST_F(PyrowaveSessionTest, RejectsInvalidInputsBeforeCallingTheCodec) {
  EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), nullptr, fake_pyrowave_api()), nullptr);
  EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), std::make_unique<platf::avcodec_encode_device_t>(), fake_pyrowave_api()), nullptr);
  for (int dimension : {32, 96}) {
    auto converter = std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12);
    converter->frame->width = dimension;
    EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), std::move(converter), fake_pyrowave_api()), nullptr);
  }
  auto config = pyrowave_config();
  config.chromaSamplingType = 2;
  EXPECT_EQ(video::pyrowave::make_session(config, std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12), fake_pyrowave_api()), nullptr);

  auto converter = std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12);
  converter->frame->format = AV_PIX_FMT_VIDEOTOOLBOX;
  EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), std::move(converter), fake_pyrowave_api()), nullptr);

  converter = std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12);
  converter->frame->format = AV_PIX_FMT_VIDEOTOOLBOX;
  video::avcodec_buffer_t frames {av_buffer_alloc(1)};
  ASSERT_NE(frames, nullptr);
  const auto restore_allocations = util::fail_guard([]() {
    av_max_alloc(std::numeric_limits<int>::max());
  });
  av_max_alloc(0);
  EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), std::move(converter), fake_pyrowave_api(), std::move(frames)), nullptr);
  EXPECT_TRUE(state.destroyed.empty());
}

TEST_F(PyrowaveSessionTest, CleansUpPartialInitializationAndRejectsAbiMismatch) {
  for (int failure = 0; failure < 3; ++failure) {
    state = {};
    if (failure == 0) {
      state.major = 99;
    } else if (failure == 1) {
      state.device_result = PYROWAVE_ERROR_NO_VULKAN;
    } else {
      state.encoder_result = PYROWAVE_ERROR_GENERIC;
    }
    EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12), fake_pyrowave_api()), nullptr);
    EXPECT_EQ(state.destroyed, failure == 2 ? std::vector<std::string> {"device"} : std::vector<std::string> {});
  }
}

TEST_F(PyrowaveSessionTest, FailsClosedOnEncodeAndPacketizationErrors) {
  auto session = video::pyrowave::make_session(pyrowave_config(), std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12), fake_pyrowave_api());
  ASSERT_NE(session, nullptr);
  platf::img_t image {};
  ASSERT_EQ(session->convert(image), 0);
  for (int failure = 0; failure < 12; ++failure) {
    state = {};
    switch (failure) {
      case 0:
        state.encode_result = PYROWAVE_ERROR_GENERIC;
        break;
      case 1:
        state.count_result = PYROWAVE_ERROR_GENERIC;
        break;
      case 2:
        state.count = 0;
        break;
      case 3:
        state.count = 65536;
        break;
      case 4:
        state.count = video::pyrowave::max_frame_size / video::pyrowave::packet_boundary + 1;
        break;
      case 5:
        state.packet_result = PYROWAVE_ERROR_GENERIC;
        break;
      case 6:
        state.output_count = 0;
        break;
      case 7:
        state.output_count = 3;
        break;
      case 8:
        state.packet_size = 0;
        break;
      case 9:
        state.packet_size = video::pyrowave::packet_boundary + 1;
        break;
      case 10:
        state.packet_offset = std::numeric_limits<size_t>::max();
        break;
      case 11:
        state.packet_offset = 2 * video::pyrowave::packet_boundary - 1;
        break;
    }
    EXPECT_EQ(session->encode(failure), nullptr) << failure;
  }
}

/**
 * @brief Run integration tests only when the real device supports PyroWave's encoder shaders.
 */
class PyrowaveRoundTripTest: public testing::Test {
protected:
  /**
   * @brief Check shader capabilities, which are stricter than Vulkan device creation.
   */
  void SetUp() override {
    if (pyrowave_create_default_device(&device) != PYROWAVE_SUCCESS) {
      GTEST_SKIP() << "A PyroWave-compatible Vulkan device is unavailable";
    }
    pyrowave_encoder encoder {};
    const pyrowave_encoder_create_info info {device, 64, 48, PYROWAVE_CHROMA_SUBSAMPLING_420};
    if (pyrowave_encoder_create(&info, &encoder) != PYROWAVE_SUCCESS) {
      GTEST_SKIP() << "The Vulkan device does not support PyroWave's encoder shaders";
    }
    pyrowave_encoder_destroy(encoder);
  }

  /**
   * @brief Release the codec device after the test or a skipped capability probe.
   */
  void TearDown() override {
    if (device) {
      pyrowave_device_destroy(device);
    }
  }

  pyrowave_device device {};  ///< Real Vulkan codec device retained for decoder tests.
};

TEST_F(PyrowaveSessionTest, ConvertsCapturedRgbThroughTheSoftwareBackend) {
  state.solid_color = std::array<uint16_t, 3> {127, 128, 128};
  std::vector<uint8_t> pixels(128 * 96 * 4, 127);
  platf::img_t image {};
  image.data = pixels.data();
  image.width = 128;
  image.height = 96;
  image.pixel_pitch = 4;
  image.row_pitch = 128 * 4;
  for (int chroma : {0, 1}) {
    auto config = pyrowave_config();
    config.chromaSamplingType = chroma;
    auto converter = std::make_unique<platf::avcodec_encode_device_t>();
    converter->colorspace = video::colorspace_from_client_config(config, false);
    auto session = video::make_pyrowave_encode_session(video::software, config, 128, 96, std::move(converter), fake_pyrowave_api());
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(session->convert(image), 0);
    auto pyrowave_session = dynamic_cast<video::pyrowave::session_t *>(session.get());
    ASSERT_NE(pyrowave_session, nullptr);
    EXPECT_NE(pyrowave_session->encode(1), nullptr);
    EXPECT_EQ(state.chroma, chroma ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420);
  }
}

#ifdef SUNSHINE_BUILD_VULKAN
namespace {
  /**
   * @brief Upload prepared pixels through a borrowed Vulkan frame pool, like platform converters.
   */
  class pyrowave_test_upload_t: public pyrowave_test_converter_t {
  public:
    /**
     * @brief Prepare SDR or HDR host pixels for upload.
     * @param format Input pixel layout.
     */
    explicit pyrowave_test_upload_t(AVPixelFormat format):
        pyrowave_test_converter_t(format) {
      hardware = true;
    }

    /**
     * @brief Request the same image usage as the Vulkan video converter.
     * @param frames Hardware frame configuration.
     */
    void init_hwframes(AVHWFramesContext *frames) override {
      auto vk = static_cast<AVVulkanFramesContext *>(frames->hwctx);
      vk->usage = static_cast<VkImageUsageFlagBits>(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);
    }

    /**
     * @brief Take ownership of the converted frame and borrow its pool.
     * @param new_frame Empty encoder input frame.
     * @param hw_frames_ctx Retained by the session.
     * @return Zero on success.
     */
    int set_frame(AVFrame *new_frame, AVBufferRef *hw_frames_ctx) override {
      input = std::move(owned);
      owned.reset(new_frame);
      frame = owned.get();
      frames = hw_frames_ctx;
      EXPECT_EQ(frame->color_trc, AVCOL_TRC_SMPTE2084);
      const auto pool = reinterpret_cast<AVHWFramesContext *>(frames->data);
      const auto vk = static_cast<AVVulkanFramesContext *>(pool->hwctx);
      EXPECT_TRUE(vk->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
      EXPECT_FALSE(vk->usage & VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);
      return 0;
    }

    /**
     * @brief Allocate lazily from the borrowed pool and upload the prepared image.
     * @param img Unused captured image.
     * @return FFmpeg transfer status.
     */
    int convert(platf::img_t &img) override {
      if (!frame->buf[0] && av_hwframe_get_buffer(frames, frame, 0) < 0) {
        return -1;
      }
      return av_hwframe_transfer_data(frame, input.get(), 0);
    }

  private:
    video::avcodec_frame_t input;  ///< Prepared host pixels.
    AVBufferRef *frames = nullptr;  ///< Borrowed pool used after session creation returns.
  };
}  // namespace

TEST_F(PyrowaveSessionTest, CreatesSessionsWithoutHardwareVideoEncodeUsage) {
  AVBufferRef *raw_device = nullptr;
  if (av_hwdevice_ctx_create(&raw_device, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0) < 0) {
    GTEST_SKIP() << "An FFmpeg-compatible Vulkan device is unavailable";
  }
  video::avcodec_buffer_t device {raw_device};
  video::encoder_t encoder {
    .platform_formats = std::make_unique<video::encoder_platform_formats_avcodec>(
      AV_HWDEVICE_TYPE_VULKAN,
      AV_HWDEVICE_TYPE_NONE,
      AV_PIX_FMT_VULKAN,
      AV_PIX_FMT_NV12,
      AV_PIX_FMT_P010,
      AV_PIX_FMT_YUV444P,
      AV_PIX_FMT_YUV444P16,
      [&](platf::avcodec_encode_device_t *) -> util::Either<video::avcodec_buffer_t, int> {
        return video::avcodec_buffer_t {av_buffer_ref(device.get())};
      }
    )
  };
  for (auto format : {AV_PIX_FMT_NV12, AV_PIX_FMT_P010, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P16}) {
    SCOPED_TRACE(av_get_pix_fmt_name(format));
    const auto desc = av_pix_fmt_desc_get(format);
    auto config = pyrowave_config();
    config.dynamicRange = desc->comp[0].depth > 8;
    config.chromaSamplingType = desc->log2_chroma_w == 0;
    auto session = video::make_pyrowave_encode_session(encoder, config, 64, 48, std::make_unique<pyrowave_test_upload_t>(format), fake_pyrowave_api());
    ASSERT_NE(session, nullptr);
    platf::img_t image {};
    ASSERT_EQ(session->convert(image), 0);
    auto pyrowave_session = dynamic_cast<video::pyrowave::session_t *>(session.get());
    ASSERT_NE(pyrowave_session, nullptr);
    EXPECT_NE(pyrowave_session->encode(1), nullptr);
    EXPECT_EQ(state.chroma, config.chromaSamplingType ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420);
  }
}
#endif

/**
 * @brief Exercise the real codec when a suitable Vulkan device is present.
 */
TEST_F(PyrowaveRoundTripTest, DecodesSdrAndHdrFrameContainersWithTheUpstreamLibrary) {
  bool decode_pixels = true;
#ifdef SUNSHINE_BUILD_VULKAN
  VkPhysicalDevice physical_device;
  pyrowave_device_get_vk_device_handles(device, nullptr, &physical_device, nullptr);
  VkPhysicalDeviceProperties properties {};
  vkGetPhysicalDeviceProperties(physical_device, &properties);
  // Lavapipe's decoder shader crash also reproduces in PyroWave's own tests.
  // Still validate native frame containers on software Vulkan devices.
  decode_pixels = properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU;
#endif
  for (int profile = 0; profile < 4; ++profile) {
    const bool hdr = profile / 2;
    const bool yuv444 = profile % 2;
    pyrowave_decoder decoder {};
    const pyrowave_decoder_create_info info {device, 64, 48, yuv444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420, false};
    ASSERT_EQ(pyrowave_decoder_create(&info, &decoder), PYROWAVE_SUCCESS);
    const auto destroy_decoder = util::fail_guard([&]() {
      pyrowave_decoder_destroy(decoder);
    });
    auto config = pyrowave_config();
    config.dynamicRange = hdr;
    config.chromaSamplingType = yuv444;
    const auto format = yuv444 ? (hdr ? AV_PIX_FMT_YUV444P10 : AV_PIX_FMT_YUV444P) : (hdr ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12);
    auto session = video::pyrowave::make_session(config, std::make_unique<pyrowave_test_converter_t>(format));
    ASSERT_NE(session, nullptr);
    platf::img_t image {};
    ASSERT_EQ(session->convert(image), 0);
    auto packet = session->encode(1);
    ASSERT_NE(packet, nullptr);
    const auto data = packet->data();
    ASSERT_GE(packet->data_size(), 8);
    ASSERT_EQ(std::string(reinterpret_cast<char *>(data), 4), "PYRW");
    ASSERT_EQ(data[4], 1);
    const auto count = (uint16_t(data[5]) << 8) | data[6];
    size_t offset = 8;
    for (int i = 0; i < count; ++i) {
      ASSERT_LE(offset + 4, packet->data_size());
      const auto size = (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
                        (uint32_t(data[offset + 2]) << 8) | data[offset + 3];
      offset += 4;
      ASSERT_LE(size, packet->data_size() - offset);
      ASSERT_EQ(pyrowave_decoder_push_packet(decoder, data + offset, size), PYROWAVE_SUCCESS);
      offset += size;
    }
    EXPECT_EQ(offset, packet->data_size());
    ASSERT_TRUE(pyrowave_decoder_decode_is_ready(decoder, false));
    if (!decode_pixels) {
      continue;
    }
    std::array<std::vector<uint16_t>, 3> output;
    pyrowave_cpu_buffer decoded {};
    decoded.width = 64;
    decoded.height = 48;
    decoded.format = yuv444 ? PYROWAVE_CPU_BUFFER_FORMAT_YUV444P10 : PYROWAVE_CPU_BUFFER_FORMAT_YUV420P10;
    for (int plane = 0; plane < 3; ++plane) {
      output[plane].resize(plane && !yuv444 ? 32 * 24 : 64 * 48);
      decoded.data[plane] = output[plane].data();
      decoded.row_stride_in_bytes[plane] = (plane && !yuv444 ? 32 : 64) * sizeof(uint16_t);
      decoded.plane_size_in_bytes[plane] = output[plane].size() * sizeof(uint16_t);
    }
    ASSERT_EQ(pyrowave_decoder_decode_cpu_buffer_synchronous(decoder, &decoded), PYROWAVE_SUCCESS);
    EXPECT_NEAR(output[0][0], hdr ? 1 : 4, 8);
    EXPECT_NEAR(output[0][1], 1023, 8);
    EXPECT_NEAR(output[1][0], hdr ? 1 : 4, 8);
    EXPECT_NEAR(output[2][0], 1023, 8);
    EXPECT_NEAR(output[1][1], 1023, 8);
    EXPECT_NEAR(output[2][1], hdr ? 1 : 4, 8);
  }
  if (!decode_pixels) {
    GTEST_SKIP() << "SDR/HDR packet validation passed; pixel decoding requires a hardware Vulkan device";
  }
}
