/**
 * @file tests/unit/test_video_pyrowave.cpp
 * @brief Regression coverage for PyroWave profiles, HDR precision, framing, and failures.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <cstring>
#include <limits>

// lib includes
extern "C" {
#include <libavutil/hwcontext.h>
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

TEST(PyrowaveConfigTest, ServerInfoReportsSdrAndHdrIndependently) {
  const auto previous = video::active_pyrowave_mode;
  const auto restore = util::fail_guard([previous]() {
    video::active_pyrowave_mode = previous;
  });
  video::active_pyrowave_mode = 0;
  const auto baseline = nvhttp::get_codec_mode_flags();
  EXPECT_EQ(baseline & (video::pyrowave::scm_sdr | video::pyrowave::scm_hdr), 0);
  video::active_pyrowave_mode = 2;
  EXPECT_EQ(nvhttp::get_codec_mode_flags(), baseline | video::pyrowave::scm_sdr);
  video::active_pyrowave_mode = 3;
  EXPECT_EQ(nvhttp::get_codec_mode_flags(), baseline | video::pyrowave::scm_sdr | video::pyrowave::scm_hdr);
}

namespace {
  /**
   * @brief Return a small, valid PyroWave configuration for deterministic tests.
   * @return 64x48 SDR 4:2:0 at 60 FPS.
   */
  video::config_t pyrowave_config() {
    return {64, 48, 60, 6000, 200000, 1, 0, 3, video::pyrowave::video_format, 0, 0, 0};
  }
}  // namespace

TEST(PyrowaveConfigTest, NegotiatesOnlySuccessfullyProbedProfiles) {
  auto config = pyrowave_config();
  for (int mode : {0, 1, 2, 3, 4}) {
    config.dynamicRange = 0;
    EXPECT_EQ(video::pyrowave::supported(config, mode), mode == 2 || mode == 3);
    config.dynamicRange = 1;
    EXPECT_EQ(video::pyrowave::supported(config, mode), mode == 3);
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
    {&video::config_t::chromaSamplingType, 1},
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
    EXPECT_FALSE(video::pyrowave::supported(config, 3)) << value;
  }
}

TEST(PyrowaveConfigTest, BudgetsFractionalRatesWithoutIntegerOverflow) {
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
}

TEST(PyrowaveConfigTest, PreservesHdrWithoutConsultingUnrelatedCodecCapabilities) {
  auto config = pyrowave_config();
  config.dynamicRange = 1;
  video::encoder_t encoder {};
  EXPECT_EQ(video::resolve_dynamic_range(encoder, config).dynamicRange, 1);
}

TEST(PyrowaveConfigTest, DisablingTheCodecClearsPreviouslyProbedProfiles) {
  const auto previous_mode = video::active_pyrowave_mode;
  const auto previous_enabled = config::video.pyrowave;
  const auto restore = util::fail_guard([&]() {
    video::active_pyrowave_mode = previous_mode;
    config::video.pyrowave = previous_enabled;
  });
  video::active_pyrowave_mode = 3;
  config::video.pyrowave = false;
  video::probe_pyrowave({});
  EXPECT_EQ(video::active_pyrowave_mode, 0);
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
   * @brief Converter supplying padded NV12/P010 frames without display hardware.
   */
  class pyrowave_test_converter_t: public platf::avcodec_encode_device_t {
  public:
    /**
     * @brief Allocate a frame and fill full-range endpoint values.
     * @param format Pixel storage layout supplied by the capture converter.
     */
    explicit pyrowave_test_converter_t(AVPixelFormat format) {
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
      const bool planar = format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUV420P10;
      for (int plane = 0; plane < (planar ? 3 : 2); ++plane) {
        for (int y = 0; y < (plane ? 24 : 48); ++y) {
          auto row = frame->data[plane] + y * frame->linesize[plane];
          const auto width = planar && plane ? 32 : 64;
          if (format == AV_PIX_FMT_P010 || format == AV_PIX_FMT_YUV420P10) {
            auto words = reinterpret_cast<uint16_t *>(row);
            for (int x = 0; x < width; ++x) {
              const auto value = (planar && plane ? plane == 2 : x % 2) ? 1023 : 1;
              words[x] = uint16_t(value << (format == AV_PIX_FMT_P010 ? 6 : 0));
            }
          } else {
            for (int x = 0; x < width; ++x) {
              row[x] = (planar && plane ? plane == 2 : x % 2) ? 255 : 1;
            }
          }
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
    pyrowave_cpu_buffer_format format {};  ///< Last codec input format.
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
      EXPECT_EQ(info->chroma, PYROWAVE_CHROMA_SUBSAMPLING_420);
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
      api_state->format = input->format;
      for (int plane = 0; plane < 3; ++plane) {
        EXPECT_GE(input->row_stride_in_bytes[plane], plane ? 32 : 64);
        EXPECT_EQ(input->plane_size_in_bytes[plane], input->row_stride_in_bytes[plane] * (plane ? 24 : 48));
      }
      if (input->format == PYROWAVE_CPU_BUFFER_FORMAT_YUV420P10) {
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

TEST_F(PyrowaveSessionTest, RetainsTheBorrowedHardwarePoolUntilAfterConverterDestruction) {
  bool released = false;
  video::avcodec_buffer_t frames {av_buffer_create(nullptr, 0, [](void *opaque, uint8_t *) {
    *static_cast<bool *>(opaque) = true;
  },
                                                   &released,
                                                   0)};
  ASSERT_NE(frames, nullptr);
  auto converter = std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12);
  converter->on_destroy = [&]() {
    EXPECT_FALSE(released);
  };
  auto session = video::pyrowave::make_session(pyrowave_config(), std::move(converter), fake_pyrowave_api(), std::move(frames));
  ASSERT_NE(session, nullptr);
  EXPECT_FALSE(released);
  platf::img_t image {};
  EXPECT_EQ(session->convert(image), 0);
  EXPECT_FALSE(released);
  session.reset();
  EXPECT_TRUE(released);
}

TEST_F(PyrowaveSessionTest, EncodesSdrAndHdrWithExactPlaneValuesAndFraming) {
  for (auto format : {AV_PIX_FMT_NV12, AV_PIX_FMT_P010, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10}) {
    const bool hdr = format == AV_PIX_FMT_P010 || format == AV_PIX_FMT_YUV420P10;
    auto config = pyrowave_config();
    config.dynamicRange = hdr;
    auto converter = std::make_unique<pyrowave_test_converter_t>(format);
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
    EXPECT_EQ(state.format, hdr ? PYROWAVE_CPU_BUFFER_FORMAT_YUV420P10 : PYROWAVE_CPU_BUFFER_FORMAT_YUV420P);
    EXPECT_EQ(state.samples, (std::array<uint16_t, 4> {1, uint16_t(hdr ? 1023 : 255), 1, uint16_t(hdr ? 1023 : 255)}));
    // Repeated capture content must be encoded again so the codec sequence advances.
    EXPECT_NE(session->encode(43), nullptr);
  }
  EXPECT_EQ(state.encoded, 8);
  EXPECT_EQ(state.destroyed, (std::vector<std::string> {"encoder", "device", "encoder", "device", "encoder", "device", "encoder", "device"}));
}

TEST_F(PyrowaveSessionTest, DownloadsHardwareFramesWithoutLosingTenBitPrecision) {
  AVBufferRef *raw_device = nullptr;
  if (av_hwdevice_ctx_create(&raw_device, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0) < 0) {
    GTEST_SKIP() << "An FFmpeg-compatible Vulkan device is unavailable";
  }
  video::avcodec_buffer_t device {raw_device};
  for (bool hdr : {false, true}) {
    auto config = pyrowave_config();
    config.dynamicRange = hdr;
    const auto format = hdr ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
    auto converter = std::make_unique<pyrowave_test_converter_t>(format);
    auto host = std::move(converter->owned);
    converter->owned.reset(av_frame_alloc());
    converter->frame = converter->owned.get();
    ASSERT_NE(converter->frame, nullptr);
    video::avcodec_buffer_t frames {av_hwframe_ctx_alloc(device.get())};
    ASSERT_NE(frames, nullptr);
    auto pool = reinterpret_cast<AVHWFramesContext *>(frames->data);
    pool->format = AV_PIX_FMT_VULKAN;
    pool->sw_format = format;
    pool->width = 64;
    pool->height = 48;
    ASSERT_EQ(av_hwframe_ctx_init(frames.get()), 0);
    ASSERT_EQ(av_hwframe_get_buffer(frames.get(), converter->frame, 0), 0);
    ASSERT_EQ(av_hwframe_transfer_data(converter->frame, host.get(), 0), 0);
    ASSERT_EQ(av_frame_copy_props(converter->frame, host.get()), 0);
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

TEST_F(PyrowaveSessionTest, ClearsReadinessAfterConversionFailureAndRejectsWrongDepth) {
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
  raw->frame->format = AV_PIX_FMT_NV12;
  raw->frame->width = 0;
  EXPECT_EQ(session->convert(image), -1);
  EXPECT_EQ(session->encode(1), nullptr);
  EXPECT_EQ(state.encoded, 0);
}

TEST_F(PyrowaveSessionTest, RejectsInvalidInputsBeforeCallingTheCodec) {
  EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), nullptr, fake_pyrowave_api()), nullptr);
  EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), std::make_unique<platf::avcodec_encode_device_t>(), fake_pyrowave_api()), nullptr);
  for (int dimension : {32, 96}) {
    auto converter = std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12);
    converter->frame->width = dimension;
    EXPECT_EQ(video::pyrowave::make_session(pyrowave_config(), std::move(converter), fake_pyrowave_api()), nullptr);
  }
  auto config = pyrowave_config();
  config.chromaSamplingType = 1;
  EXPECT_EQ(video::pyrowave::make_session(config, std::make_unique<pyrowave_test_converter_t>(AV_PIX_FMT_NV12), fake_pyrowave_api()), nullptr);
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

TEST_F(PyrowaveRoundTripTest, ConvertsCapturedRgbThroughTheSoftwareBackend) {
  auto converter = std::make_unique<platf::avcodec_encode_device_t>();
  converter->colorspace = video::colorspace_from_client_config(pyrowave_config(), false);
  auto session = video::make_encode_session(nullptr, video::software, pyrowave_config(), 128, 96, std::move(converter));
  ASSERT_NE(session, nullptr);
  std::vector<uint8_t> pixels(128 * 96 * 4, 127);
  platf::img_t image {};
  image.data = pixels.data();
  image.width = 128;
  image.height = 96;
  image.pixel_pitch = 4;
  image.row_pitch = 128 * 4;
  ASSERT_EQ(session->convert(image), 0);
  auto pyrowave_session = dynamic_cast<video::pyrowave::session_t *>(session.get());
  ASSERT_NE(pyrowave_session, nullptr);
  EXPECT_NE(pyrowave_session->encode(1), nullptr);
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
     * @param format NV12 or P010 input format.
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

TEST_F(PyrowaveRoundTripTest, CreatesHdrSessionsWithoutHardwareVideoEncodeUsage) {
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
      AV_PIX_FMT_NONE,
      AV_PIX_FMT_NONE,
      [&](platf::avcodec_encode_device_t *) -> util::Either<video::avcodec_buffer_t, int> {
        return video::avcodec_buffer_t {av_buffer_ref(device.get())};
      }
    )
  };
  auto config = pyrowave_config();
  config.dynamicRange = 1;
  auto session = video::make_encode_session(nullptr, encoder, config, 64, 48, std::make_unique<pyrowave_test_upload_t>(AV_PIX_FMT_P010));
  ASSERT_NE(session, nullptr);
  platf::img_t image {};
  ASSERT_EQ(session->convert(image), 0);
  auto pyrowave_session = dynamic_cast<video::pyrowave::session_t *>(session.get());
  ASSERT_NE(pyrowave_session, nullptr);
  EXPECT_NE(pyrowave_session->encode(1), nullptr);
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
  // Still validate both native frame containers on software Vulkan devices.
  decode_pixels = properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU;
#endif
  for (bool hdr : {false, true}) {
    pyrowave_decoder decoder {};
    const pyrowave_decoder_create_info info {device, 64, 48, PYROWAVE_CHROMA_SUBSAMPLING_420, false};
    ASSERT_EQ(pyrowave_decoder_create(&info, &decoder), PYROWAVE_SUCCESS);
    const auto destroy_decoder = util::fail_guard([&]() {
      pyrowave_decoder_destroy(decoder);
    });
    auto config = pyrowave_config();
    config.dynamicRange = hdr;
    auto session = video::pyrowave::make_session(config, std::make_unique<pyrowave_test_converter_t>(hdr ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12));
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
    decoded.format = PYROWAVE_CPU_BUFFER_FORMAT_YUV420P10;
    for (int plane = 0; plane < 3; ++plane) {
      output[plane].resize(plane ? 32 * 24 : 64 * 48);
      decoded.data[plane] = output[plane].data();
      decoded.row_stride_in_bytes[plane] = (plane ? 32 : 64) * sizeof(uint16_t);
      decoded.plane_size_in_bytes[plane] = output[plane].size() * sizeof(uint16_t);
    }
    ASSERT_EQ(pyrowave_decoder_decode_cpu_buffer_synchronous(decoder, &decoded), PYROWAVE_SUCCESS);
    EXPECT_NEAR(output[0][0], hdr ? 1 : 4, 8);
    EXPECT_NEAR(output[0][1], 1023, 8);
    EXPECT_NEAR(output[1][0], hdr ? 1 : 4, 8);
    EXPECT_NEAR(output[2][0], 1023, 8);
  }
  if (!decode_pixels) {
    GTEST_SKIP() << "SDR/HDR packet validation passed; pixel decoding requires a hardware Vulkan device";
  }
}
