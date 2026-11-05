/**
 * @file tests/integration/test_x11_encode_e2e.cpp
 * @brief End-to-end test: real X11 capture, software conversion, H.264 encode round trip.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_X11)

  // test includes
  #include "../tests_common.h"
  #include "x11_capture_helpers.h"
  #include "xvfb_server.h"

  // standard includes
  #include <array>
  #include <bitset>
  #include <cstddef>
  #include <cstdint>
  #include <memory>
  #include <optional>
  #include <string>
  #include <vector>

  // platform includes
  #include <X11/Xlib.h>

// ffmpeg includes
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavutil/frame.h>
  #include <libswscale/swscale.h>
}

  // local includes
  #include <src/globals.h>
  #include <src/platform/common.h>
  #include <src/video.h>

namespace stream {
  struct session_t;
}  // namespace stream

namespace platf {
  extern std::shared_ptr<display_t> x11_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);
}  // namespace platf

namespace video {
  std::unique_ptr<platf::encode_device_t> make_encode_device(platf::display_t &disp, const encoder_t &encoder, const config_t &config);
  std::unique_ptr<encode_session_t> make_encode_session(platf::display_t *disp, const encoder_t &encoder, const config_t &config, int width, int height, std::unique_ptr<platf::encode_device_t> encode_device);
  int encode(int64_t frame_nr, encode_session_t &session, safe::mail_raw_t::queue_t<packet_t> &packets, stream::session_t *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp);
}  // namespace video

namespace {
  constexpr int kWidth = 640;  ///< Xvfb screen and encoded frame width.
  constexpr int kHeight = 480;  ///< Xvfb screen and encoded frame height.
  constexpr unsigned long kBlueGreenRed = 0x336699;  ///< R=0x33, G=0x66, B=0x99 in the canonical TrueColor masks.
  constexpr unsigned long kRed = 0xFF0000;  ///< Pure red desktop.
  constexpr int kColorTolerance = 24;  ///< Lossy round-trip tolerance per channel.
  constexpr int kMaxEncodeAttempts = 30;  ///< Encode calls allowed before a packet must be available.

  /// Owning FFmpeg software scaler context.
  using sws_ptr = std::unique_ptr<SwsContext, decltype(&sws_freeContext)>;

  /**
   * @brief Release an FFmpeg frame.
   */
  struct avframe_deleter {
    /**
     * @brief Free the frame.
     *
     * @param frame Frame to free.
     */
    void operator()(AVFrame *frame) const {
      av_frame_free(&frame);
    }
  };

  /**
   * @brief Release an FFmpeg codec context.
   */
  struct avcodec_ctx_deleter {
    /**
     * @brief Free the codec context.
     *
     * @param ctx Codec context to free.
     */
    void operator()(AVCodecContext *ctx) const {
      avcodec_free_context(&ctx);
    }
  };

  /// Owning FFmpeg frame.
  using avframe_ptr = std::unique_ptr<AVFrame, avframe_deleter>;
  /// Owning FFmpeg codec context.
  using avcodec_ctx_ptr = std::unique_ptr<AVCodecContext, avcodec_ctx_deleter>;

  /**
   * @brief Restores a codec capability set when the scope ends.
   */
  class capabilities_guard_t {
  public:
    /**
     * @brief Save the capabilities of a codec before they are mutated.
     *
     * @param capabilities Codec capabilities to restore on destruction.
     */
    explicit capabilities_guard_t(std::bitset<video::encoder_t::MAX_FLAGS> &capabilities):
        capabilities_(capabilities),
        previous_(capabilities) {}

    ~capabilities_guard_t() {
      capabilities_ = previous_;
    }

    capabilities_guard_t(const capabilities_guard_t &) = delete;
    capabilities_guard_t &operator=(const capabilities_guard_t &) = delete;

  private:
    std::bitset<video::encoder_t::MAX_FLAGS> &capabilities_;  ///< Capabilities to restore.
    std::bitset<video::encoder_t::MAX_FLAGS> previous_;  ///< Saved capabilities.
  };

  /**
   * @brief Create an H.264 decoder for a stream of packets.
   *
   * @return Decoder context, or null when the decoder is not available.
   */
  avcodec_ctx_ptr make_h264_decoder() {
    const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!decoder) {
      return {nullptr};
    }

    avcodec_ctx_ptr ctx(avcodec_alloc_context3(decoder));
    if (!ctx) {
      return {nullptr};
    }

    // Decode packet-by-packet without frame threading so one packet yields one
    // frame deterministically; frame-level delay would turn the first receive
    // into EAGAIN depending on the CPU count.
    ctx->thread_count = 1;
    if (avcodec_open2(ctx.get(), decoder, nullptr) < 0) {
      return {nullptr};
    }
    return ctx;
  }

  /**
   * @brief Decode one H.264 packet and convert the frame back to packed BGRA.
   *
   * @param ctx Decoder context already fed with the stream's IDR packet.
   * @param packet Encoded H.264 packet.
   * @return Tightly packed BGRA bytes, or null when decoding failed.
   */
  std::optional<std::vector<std::uint8_t>> decode_h264_to_bgra(AVCodecContext *ctx, const AVPacket *packet) {
    if (avcodec_send_packet(ctx, packet) < 0) {
      return std::nullopt;
    }

    avframe_ptr frame(av_frame_alloc());
    if (!frame || avcodec_receive_frame(ctx, frame.get()) < 0) {
      return std::nullopt;
    }
    if (frame->width != kWidth || frame->height != kHeight) {
      return std::nullopt;
    }

    sws_ptr sws(
      sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr),
      &sws_freeContext
    );
    if (!sws) {
      return std::nullopt;
    }

    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(kWidth) * kHeight * 4);
    std::array<std::uint8_t *, 4> dst_data {bgra.data()};
    if (std::array<int, 4> dst_linesize {kWidth * 4}; sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, dst_data.data(), dst_linesize.data()) <= 0) {
      return std::nullopt;
    }
    return bgra;
  }

  /**
   * @brief Sample the B, G and R channels at the center of a BGRA frame.
   *
   * @param bgra Tightly packed BGRA bytes.
   * @return Center pixel channels, or null when the buffer is too small.
   */
  std::optional<std::array<std::uint8_t, 3>> center_bgr(const std::vector<std::uint8_t> &bgra) {
    const std::size_t offset = static_cast<std::size_t>(kHeight / 2) * kWidth * 4 + static_cast<std::size_t>(kWidth / 2) * 4;
    if (bgra.size() < offset + 3) {
      return std::nullopt;
    }
    return std::array<std::uint8_t, 3> {bgra[offset], bgra[offset + 1], bgra[offset + 2]};
  }

  /**
   * @brief Capture one desktop frame, convert it, and return the encoded packet.
   *
   * @param xdisplay Xvfb display connection used to paint the desktop.
   * @param disp X11 display backend to capture from.
   * @param session Software encode session.
   * @param packets Queue receiving encoded packets.
   * @param color Desktop color to paint before the capture.
   * @param frame_nr Monotonic frame number for the encoder.
   * @param idr True to request an IDR frame.
   * @return Encoded packet, or null when capture or encode failed.
   */
  video::packet_t encode_single_frame(Display *xdisplay, platf::display_t &disp, video::encode_session_t &session, safe::mail_raw_t::queue_t<video::packet_t> &packets, unsigned long color, int64_t frame_nr, bool idr) {
    // Drop leftovers so the returned packet is always the one just requested.
    while (packets->peek()) {
      packets->pop();
    }

    x11_test::fill_root_window(xdisplay, color);
    if (auto captured = x11_test::capture_one_frame(disp); !captured || session.convert(*captured)) {
      return nullptr;
    }

    if (idr) {
      session.request_idr_frame();
    } else {
      session.request_normal_frame();
    }

    for (int attempt = 0; attempt < kMaxEncodeAttempts && !packets->peek(); ++attempt) {
      if (video::encode(frame_nr, session, packets, nullptr, {})) {
        return nullptr;
      }
    }

    return packets->peek() ? packets->pop() : nullptr;
  }
}  // namespace

/**
 * @brief A color drawn on a real X server survives capture, software conversion
 *        (declared BGR0), H.264 encode and decode with the right channel order.
 */
TEST(X11EncodeE2ETests, CapturesConvertsEncodesAndDecodesDesktopColors) {
  xvfb::xvfb_server_t server;
  const auto server_result = server.start(kWidth, kHeight);
  if (server_result == xvfb::start_result_e::unavailable) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Xvfb is not available";
    GTEST_SKIP() << "Xvfb is not available";
  }
  ASSERT_EQ(server_result, xvfb::start_result_e::started) << "Xvfb failed to start";

  video::config_t config {};
  config.width = kWidth;
  config.height = kHeight;
  config.framerate = 60;
  config.framerateX100 = 6000;
  config.bitrate = 4000;
  config.slicesPerFrame = 1;
  config.numRefFrames = 1;
  config.encoderCscMode = 0;
  config.videoFormat = 0;  // H.264
  config.dynamicRange = 0;
  config.chromaSamplingType = 0;  // 4:2:0
  config.enableIntraRefresh = 0;

  auto disp = platf::x11_display(platf::mem_type_e::system, "", config);
  if (!disp) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "X11 capture backend is not available";
    GTEST_SKIP() << "X11 capture backend is not available";
  }
  ASSERT_EQ(disp->width, kWidth);
  ASSERT_EQ(disp->height, kHeight);

  // The streaming pipeline normally marks the codec as probed in
  // probe_encoders(); the software H.264 path is always available in the
  // pinned FFmpeg. The capability bits are restored when the test ends so the
  // mutation does not leak into other tests sharing this binary.
  const capabilities_guard_t software_capabilities_guard {video::software.h264.capabilities};
  video::software.h264.capabilities.set();

  auto encode_device = video::make_encode_device(*disp, video::software, config);
  if (!encode_device) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "The software encode device is not available";
    GTEST_SKIP() << "The software encode device is not available";
  }

  auto session = video::make_encode_session(disp.get(), video::software, config, disp->width, disp->height, std::move(encode_device));
  if (!session) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "The software encoder session is not available";
    GTEST_SKIP() << "The software encoder session is not available";
  }

  auto packets = mail::man->queue<video::packet_t>(mail::video_packets);

  x11_test::xdisplay_ptr xdisplay(XOpenDisplay(nullptr), &XCloseDisplay);
  if (!xdisplay) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Could not open the Xvfb display";
    GTEST_SKIP() << "Could not open the Xvfb display";
  }

  // A stream decoder must see the IDR's parameter sets before later frames, so
  // the same context decodes both packets in order.
  auto decoder = make_h264_decoder();
  if (!decoder) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "The H.264 decoder is not available";
    GTEST_SKIP() << "The H.264 decoder is not available";
  }

  const auto encode_frame = [&](unsigned long color, int64_t frame_nr, bool idr) {
    return encode_single_frame(xdisplay.get(), *disp, *session, packets, color, frame_nr, idr);
  };

  // First frame: IDR carrying the blue-green-red desktop.
  auto first = encode_frame(kBlueGreenRed, 1, true);
  ASSERT_NE(first, nullptr) << "the IDR frame was not encoded";
  EXPECT_TRUE(first->is_idr());
  auto *first_avcodec = dynamic_cast<video::packet_raw_avcodec *>(first.get());
  ASSERT_NE(first_avcodec, nullptr);
  EXPECT_GT(first_avcodec->data_size(), 0u);

  auto bgr_frame = decode_h264_to_bgra(decoder.get(), first_avcodec->av_packet);
  ASSERT_TRUE(bgr_frame.has_value()) << "the encoded IDR frame did not decode";
  auto bgr_center = center_bgr(*bgr_frame);
  ASSERT_TRUE(bgr_center.has_value());
  EXPECT_NEAR((*bgr_center)[0], 0x99, kColorTolerance) << "blue channel";
  EXPECT_NEAR((*bgr_center)[1], 0x66, kColorTolerance) << "green channel";
  EXPECT_NEAR((*bgr_center)[2], 0x33, kColorTolerance) << "red channel";

  // Second frame: a normal inter frame with a different desktop color.
  auto second = encode_frame(kRed, 2, false);
  ASSERT_NE(second, nullptr) << "the P frame was not encoded";
  EXPECT_FALSE(second->is_idr());
  auto *second_avcodec = dynamic_cast<video::packet_raw_avcodec *>(second.get());
  ASSERT_NE(second_avcodec, nullptr);

  auto red_frame = decode_h264_to_bgra(decoder.get(), second_avcodec->av_packet);
  ASSERT_TRUE(red_frame.has_value()) << "the encoded P frame did not decode";
  auto red_center = center_bgr(*red_frame);
  ASSERT_TRUE(red_center.has_value());
  EXPECT_NEAR((*red_center)[0], 0x00, kColorTolerance) << "blue channel";
  EXPECT_NEAR((*red_center)[1], 0x00, kColorTolerance) << "green channel";
  EXPECT_NEAR((*red_center)[2], 0xFF, kColorTolerance) << "red channel";
}

#endif
