/**
 * @file src/video.cpp
 * @brief Definitions for video.
 */
// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <list>
#include <thread>
#include <utility>

// lib includes
#include <boost/pointer_cast.hpp>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#if !defined(_WIN32) && !defined(__APPLE__)
  #include <ffnvcodec/nvEncodeAPI.h>
#endif
}

// local includes
#include "cbs.h"
#include "config.h"
#include "display_device.h"
#include "globals.h"
#include "input.h"
#include "logging.h"
#include "nvenc/nvenc_encoder.h"
#include "platform/common.h"
#include "sync.h"
#include "video.h"
#include "video_capture_pool.h"
#include "video_probe.h"

#ifdef _WIN32
extern "C" {
  #include <libavutil/hwcontext_d3d11va.h>
}
#endif

using namespace std::literals;

namespace video {

  namespace {
    /**
     * @brief Check if we can allow probing for the encoders.
     * @return True if there should be no issues with the probing, false if we should prevent it.
     * @param displays Display configuration manager.
     */
    bool allow_encoder_probing(display_device::manager_t &displays) {
      const auto devices {displays.enumerate_devices()};

      // If there are no devices, then either the API is not working correctly or OS does not support the lib.
      // Either way we should not block the probing in this case as we can't tell what's wrong.
      if (devices.empty()) {
        return true;
      }

      // Since Windows 11 24H2, it is possible that there will be no active devices present
      // for some reason (probably a bug). Trying to probe encoders in such a state locks/breaks the DXGI
      // and also the display device for Windows. So we must have at least 1 active device.
      if (const bool at_least_one_device_is_active = std::any_of(std::begin(devices), std::end(devices), [](const auto &device) {
            // If device has additional info, it is active.
            return static_cast<bool>(device.m_info);
          });
          at_least_one_device_is_active) {
        return true;
      }

      logging::error("No display devices are active at the moment! Cannot probe the encoders.");
      return false;
    }
  }  // namespace

  /**
   * @brief Release context resources.
   */
  void free_ctx(AVCodecContext *ctx) {
    avcodec_free_context(&ctx);
  }

  /**
   * @brief Release an FFmpeg frame allocated by the capture or conversion backend.
   */
  void free_frame(AVFrame *frame) {
    av_frame_free(&frame);
  }

  /**
   * @brief Release a backend buffer allocated for capture or conversion.
   */
  void free_buffer(AVBufferRef *ref) {
    av_buffer_unref(&ref);
  }

  namespace nv {

    /**
     * @brief Enumerates supported profile h264 options.
     */
    enum class profile_h264_e : int {
      high = 2,  ///< High profile
      high_444p = 3,  ///< High 4:4:4 Predictive profile
    };

    /**
     * @brief Enumerates supported profile HEVC options.
     */
    enum class profile_hevc_e : int {
      main = 0,  ///< Main profile
      main_10 = 1,  ///< Main 10 profile
      rext = 2,  ///< Rext profile
    };

  }  // namespace nv

  namespace qsv {

    /**
     * @brief Enumerates supported profile h264 options.
     */
    enum class profile_h264_e : int {
      high = 100,  ///< High profile
      high_444p = 244,  ///< High 4:4:4 Predictive profile
    };

    /**
     * @brief Enumerates supported profile HEVC options.
     */
    enum class profile_hevc_e : int {
      main = 1,  ///< Main profile
      main_10 = 2,  ///< Main 10 profile
      rext = 4,  ///< RExt profile
    };

    /**
     * @brief Enumerates supported profile AV1 options.
     */
    enum class profile_av1_e : int {
      main = 1,  ///< Main profile
      high = 2,  ///< High profile
    };

  }  // namespace qsv

  int select_h264_profile(std::string_view encoder_name, const config_t &config, int amd_coder) {
    if (config.chromaSamplingType == 1) {
      return AV_PROFILE_H264_HIGH_444_PREDICTIVE;
    }

    if (encoder_name == "h264_amf"sv && amd_coder == std::to_underlying(amf::coder_e::cavlc)) {
      return AV_PROFILE_H264_CONSTRAINED_BASELINE;
    }

    return AV_PROFILE_H264_HIGH;
  }

  /**
   * @brief Resolve a client-requested dynamic range against probed encoder capabilities.
   *
   * @param encoder Selected encoder and its probed codec capabilities.
   * @param config Client-requested stream configuration.
   * @return Effective stream configuration, downgraded to SDR when HDR is unsupported.
   */
  config_t resolve_dynamic_range(const encoder_t &encoder, config_t config) {
    if (!config.dynamicRange) {
      return config;
    }

    const auto &video_format = encoder.codec_from_config(config);
    const auto capability = config.chromaSamplingType == 1 ?
                              encoder_t::DYNAMIC_RANGE_YUV444 :
                              encoder_t::DYNAMIC_RANGE;
    if (video_format[capability]) {
      return config;
    }

    const auto mode = config.chromaSamplingType == 1 ? "YUV 4:4:4 dynamic range"sv : "dynamic range"sv;
    logging::warning("{}: {} not supported, falling back to SDR", video_format.name, mode);
    config.dynamicRange = 0;
    return config;
  }

  /**
   * @brief Create an FFmpeg hardware device buffer for D3D11VA input.
   *
   * @param encode_device Encode device.
   * @return Hardware buffer on success, or an error code on failure.
   */
  util::Either<avcodec_buffer_t, int> dxgi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *);
  /**
   * @brief Create an FFmpeg hardware device buffer for VA-API input.
   *
   * @param settings Host render-device selection settings.
   * @param encode_device Encode device.
   * @return Hardware buffer on success, or an error code on failure.
   */
  util::Either<avcodec_buffer_t, int> vaapi_init_avcodec_hardware_input_buffer(const config::settings_t &settings, platf::avcodec_encode_device_t *encode_device);
  /**
   * @brief Create an FFmpeg hardware device buffer for CUDA input.
   *
   * @note The uniform backend hook supplies an encode device, which this initializer does not need.
   * @return Hardware buffer on success, or an error code on failure.
   */
  util::Either<avcodec_buffer_t, int> cuda_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *);
  /**
   * @brief Create an FFmpeg hardware device buffer for VideoToolbox input.
   *
   * @note The uniform backend hook supplies an encode device, which this initializer does not need.
   * @return Hardware buffer on success, or an error code on failure.
   */
  util::Either<avcodec_buffer_t, int> vt_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *);

  /**
   * @brief Copy scaled pixel planes into an aspect-ratio padded software frame.
   * @param destination Final frame retaining black padding around the scaled content.
   * @param source Scaled frame whose plane rows are copied without their trailing stride.
   * @param offset_x Horizontal content origin in full-resolution pixels.
   * @param offset_y Vertical content origin in full-resolution pixels.
   */
  void copy_scaled_frame(AVFrame &destination, const AVFrame &source, int offset_x, int offset_y) {
    auto fmt_desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(source.format));
    auto planes = av_pix_fmt_count_planes(static_cast<AVPixelFormat>(source.format));
    for (int plane = 0; plane < planes; plane++) {
      auto shift_h = plane == 0 ? 0 : fmt_desc->log2_chroma_h;
      auto shift_w = plane == 0 ? 0 : fmt_desc->log2_chroma_w;
      auto offset = ((offset_x >> shift_w) * fmt_desc->comp[plane].step) + (offset_y >> shift_h) * destination.linesize[plane];

      // Copy line-by-line to preserve leading padding for each row
      for (int line = 0; line < source.height >> shift_h; line++) {
        memcpy(destination.data[plane] + offset + (line * destination.linesize[plane]), source.data[plane] + (line * source.linesize[plane]), static_cast<std::size_t>(source.width >> shift_w) * fmt_desc->comp[plane].step);
      }
    }
  }

  /**
   * @brief Create an FFmpeg hardware device buffer for Vulkan input.
   *
   * @param settings Host render-device selection settings.
   * @param encode_device Native encoding device to initialize.
   * @return Hardware buffer on success, or an error code on failure.
   */
  util::Either<avcodec_buffer_t, int> vulkan_init_avcodec_hardware_input_buffer(const config::settings_t &settings, platf::avcodec_encode_device_t *encode_device);

  int avcodec_software_encode_device_t::convert(platf::img_t &img) {
    // If we need to add aspect ratio padding, we need to scale into an intermediate output buffer
    bool requires_padding = (sw_frame->width != sws_output_frame->width || sw_frame->height != sws_output_frame->height);

    // Detect the actual capture pixel format. PipeWire-based captures (KWin
    // screencast / XDG portal) deliver NV12 with 1 byte per pixel, while
    // KMS/DMABUF captures deliver BGR0 (4 bytes per pixel). The capture
    // backend reports bytes per pixel in img.pixel_pitch; fall back to the row
    // pitch heuristic when it is unavailable.
    const auto pixel_pitch = img.pixel_pitch > 0 ? img.pixel_pitch : (img.row_pitch / std::max(img.width, 1));
    const auto input_fmt = (pixel_pitch == 1) ? AV_PIX_FMT_NV12 : AV_PIX_FMT_BGR0;

    // The sws context is created with the default BGR0 source format;
    // recreate it once if the capture is actually NV12.
    if (input_fmt != sws_src_format) {
      sws_src_format = input_fmt;
      if (reinit_sws(input_fmt) < 0) {
        return -1;
      }
      // The colorspace details were applied to the previous sws context.
      apply_colorspace();
    }

    // Setup the input frame using the caller's img_t
    sws_input_frame->data[0] = img.data;
    sws_input_frame->linesize[0] = img.row_pitch;
    if (input_fmt == AV_PIX_FMT_NV12) {
      sws_input_frame->data[1] = img.data + static_cast<std::size_t>(img.row_pitch) * img.height;
      sws_input_frame->linesize[1] = img.row_pitch;
    } else {
      sws_input_frame->data[1] = nullptr;
      sws_input_frame->linesize[1] = 0;
    }
    sws_input_frame->data[2] = nullptr;
    sws_input_frame->linesize[2] = 0;
    sws_input_frame->data[3] = nullptr;
    sws_input_frame->linesize[3] = 0;

    // Perform color conversion and scaling to the final size
    if (auto status = sws_scale_frame(sws.get(), requires_padding ? sws_output_frame.get() : sw_frame.get(), sws_input_frame.get()); status < 0) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> string;
      logging::error([&string, &status](std::ostream &out) {
        out << "Couldn't scale frame: "sv << av_make_error_string(string.data(), AV_ERROR_MAX_STRING_SIZE, status);
      });
      return -1;
    }

    // If we require aspect ratio padding, copy the output frame into the final padded frame
    if (requires_padding) {
      copy_scaled_frame(*sw_frame, *sws_output_frame, offsetW, offsetH);
    }

    // If frame is not a software frame, it means we still need to transfer from main memory
    // to vram memory
    if (frame->hw_frames_ctx) {
      auto status = av_hwframe_transfer_data(frame, sw_frame.get(), 0);
      if (status < 0) {
        std::array<char, AV_ERROR_MAX_STRING_SIZE> string;
        logging::error([&string, &status](std::ostream &out) {
          out << "Failed to transfer image data to hardware frame: "sv << av_make_error_string(string.data(), AV_ERROR_MAX_STRING_SIZE, status);
        });
        return -1;
      }
    }

    return 0;
  }

  int avcodec_software_encode_device_t::set_frame(AVFrame *in_frame, AVBufferRef *hw_frames_ctx) {
    this->frame = in_frame;

    // If it's a hwframe, allocate buffers for hardware
    if (hw_frames_ctx) {
      hw_frame.reset(in_frame);

      if (av_hwframe_get_buffer(hw_frames_ctx, in_frame, 0)) {
        return -1;
      }
    } else {
      sw_frame.reset(in_frame);
    }

    return 0;
  }

  void avcodec_software_encode_device_t::apply_colorspace() {
    auto avcodec_colorspace = avcodec_colorspace_from_sunshine_colorspace(colorspace);
    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 0, sws_getCoefficients(avcodec_colorspace.software_format), avcodec_colorspace.range - 1, 0, 1 << 16, 1 << 16);
  }

  void avcodec_software_encode_device_t::prefill() {
    auto active_frame = sw_frame ? sw_frame.get() : this->frame;
    av_frame_get_buffer(active_frame, 0);
    av_frame_make_writable(active_frame);
    std::array<ptrdiff_t, 4> linesize = {active_frame->linesize[0], active_frame->linesize[1], active_frame->linesize[2], active_frame->linesize[3]};
    av_image_fill_black(active_frame->data, linesize.data(), static_cast<AVPixelFormat>(active_frame->format), active_frame->color_range, active_frame->width, active_frame->height);
  }

  int avcodec_software_encode_device_t::init(int in_width, int in_height, AVFrame *in_frame, AVPixelFormat format, bool hardware) {
    // If the device used is hardware, yet the image resides on main memory
    if (hardware) {
      sw_frame.reset(av_frame_alloc());

      sw_frame->width = in_frame->width;
      sw_frame->height = in_frame->height;
      sw_frame->format = format;
    } else {
      this->frame = in_frame;
    }

    // Fill aspect ratio padding in the destination frame
    prefill();

    auto out_width = in_frame->width;
    auto out_height = in_frame->height;

    // Ensure aspect ratio is maintained
    auto scalar = std::fminf(static_cast<float>(out_width) / in_width, static_cast<float>(out_height) / in_height);
    out_width = in_width * scalar;
    out_height = in_height * scalar;

    sws_input_frame.reset(av_frame_alloc());
    sws_input_frame->width = in_width;
    sws_input_frame->height = in_height;
    sws_input_frame->format = AV_PIX_FMT_BGR0;

    sws_output_frame.reset(av_frame_alloc());
    sws_output_frame->width = out_width;
    sws_output_frame->height = out_height;
    sws_output_frame->format = format;

    // Result is always positive
    offsetW = (in_frame->width - out_width) / 2;
    offsetH = (in_frame->height - out_height) / 2;

    sws_src_format = AV_PIX_FMT_BGR0;

    return reinit_sws(sws_src_format);
  }

  int avcodec_software_encode_device_t::reinit_sws(AVPixelFormat src_format) {
    sws_input_frame->format = src_format;

    sws.reset(sws_alloc_context());
    if (!sws) {
      return -1;
    }

    AVDictionary *options {nullptr};
    av_dict_set_int(&options, "srcw", sws_input_frame->width, 0);
    av_dict_set_int(&options, "srch", sws_input_frame->height, 0);
    av_dict_set_int(&options, "src_format", src_format, 0);
    av_dict_set_int(&options, "dstw", sws_output_frame->width, 0);
    av_dict_set_int(&options, "dsth", sws_output_frame->height, 0);
    av_dict_set_int(&options, "dst_format", sws_output_frame->format, 0);
    av_dict_set_int(&options, "sws_flags", SWS_LANCZOS | SWS_ACCURATE_RND, 0);
    av_dict_set_int(&options, "threads", min_threads_, 0);

    auto status = av_opt_set_dict(sws.get(), &options);
    av_dict_free(&options);
    if (status < 0) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> string;
      logging::error([&string, &status](std::ostream &out) {
        out << "Failed to set SWS options: "sv << av_make_error_string(string.data(), AV_ERROR_MAX_STRING_SIZE, status);
      });
      return -1;
    }

    status = sws_init_context(sws.get(), nullptr, nullptr);
    if (status < 0) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> string;
      logging::error([&string, &status](std::ostream &out) {
        out << "Failed to initialize SWS: "sv << av_make_error_string(string.data(), AV_ERROR_MAX_STRING_SIZE, status);
      });
      return -1;
    }

    return 0;
  }

  /**
   * @brief Enumerates supported flag options.
   */
  enum flag_e : uint32_t {
    DEFAULT = 0,  ///< Default flags
    PARALLEL_ENCODING = 1 << 1,  ///< Capture and encoding can run concurrently on separate threads
    H264_ONLY = 1 << 2,  ///< When HEVC is too heavy
    LIMITED_GOP_SIZE = 1 << 3,  ///< Some encoders don't like it when you have an infinite GOP_SIZE. e.g. VAAPI
    SINGLE_SLICE_ONLY = 1 << 4,  ///< Never use multiple slices. Older intel iGPU's ruin it for everyone else
    CBR_WITH_VBR = 1 << 5,  ///< Use a VBR rate control mode to simulate CBR
    RELAXED_COMPLIANCE = 1 << 6,  ///< Use FF_COMPLIANCE_UNOFFICIAL compliance mode
    NO_RC_BUF_LIMIT = 1 << 7,  ///< Don't set rc_buffer_size
    REF_FRAMES_INVALIDATION = 1 << 8,  ///< Support reference frames invalidation
    ALWAYS_REPROBE = 1 << 9,  ///< This is an encoder of last resort and we want to aggressively probe for a better one
    YUV444_SUPPORT = 1 << 10,  ///< Encoder may support 4:4:4 chroma sampling depending on hardware
    ASYNC_TEARDOWN = 1 << 11,  ///< Encoder supports async teardown on a different thread
    FIXED_GOP_SIZE = 1 << 12,  ///< Use fixed small GOP size (encoder doesn't support on-demand IDR frames)
  };

  /**
   * @brief FFmpeg AVCodec encode session and parameter-set rewriting state.
   */
  class avcodec_encode_session_t: public encode_session_t {
  public:
    avcodec_encode_session_t() = default;

    /**
     * @brief Initialize an FFmpeg encode session and its hardware encode device.
     *
     * @param avcodec_ctx Open FFmpeg codec context for the selected encoder.
     * @param encode_device Platform encode device that supplies frames to FFmpeg.
     * @param inject Whether SPS/VPS replacement data should be injected.
     */
    avcodec_encode_session_t(avcodec_ctx_t &&avcodec_ctx, std::unique_ptr<platf::avcodec_encode_device_t> encode_device, int inject):
        avcodec_ctx {std::move(avcodec_ctx)},
        device {std::move(encode_device)},
        inject {inject} {
    }

    /**
     * @brief Move an FFmpeg encode session without duplicating codec/device ownership.
     *
     * @param other Source object whose state is copied or moved into this object.
     */
    avcodec_encode_session_t(avcodec_encode_session_t &&other) noexcept = default;

    ~avcodec_encode_session_t() {
      // Flush any remaining frames in the encoder if the encoder started up (frame num > 0)
      if (avcodec_ctx->frame_num > 0 && avcodec_send_frame(avcodec_ctx.get(), nullptr) == 0) {
        packet_raw_avcodec pkt;
        while (avcodec_receive_packet(avcodec_ctx.get(), pkt.av_packet) == 0);
      }

      // Order matters here because the context relies on the hwdevice still being valid
      avcodec_ctx.reset();
      device.reset();
    }

    // Ensure objects are destroyed in the correct order
    /**
     * @brief Assign state from another instance while preserving ownership semantics.
     *
     * @param other Source object whose state is copied or moved into this object.
     * @return Reference or value produced by the operator.
     */
    avcodec_encode_session_t &operator=(avcodec_encode_session_t &&other) {
      device = std::move(other.device);
      avcodec_ctx = std::move(other.avcodec_ctx);
      replacements = std::move(other.replacements);
      sps = std::move(other.sps);
      vps = std::move(other.vps);

      inject = other.inject;

      return *this;
    }

    /**
     * @brief Encode one frame with FFmpeg AVCodec and prepare packet replacements.
     *
     * @param img Image or frame object to read from or populate.
     * @return Conversion status.
     */
    int convert(platf::img_t &img) override {
      if (!device) {
        return -1;
      }
      return device->convert(img);
    }

    /**
     * @brief Mark the frame as a request for an IDR frame.
     */
    void request_idr_frame() override {
      if (device && device->frame) {
        auto &frame = device->frame;
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->flags |= AV_FRAME_FLAG_KEY;
      }
    }

    /**
     * @brief Mark the frame as a request for a normal inter frame.
     */
    void request_normal_frame() override {
      if (device && device->frame) {
        auto &frame = device->frame;
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        frame->flags &= ~AV_FRAME_FLAG_KEY;
      }
    }

    /**
     * @brief Mark the frame range whose references must be invalidated.
     *
     * @param first_frame First frame.
     * @param last_frame Last frame.
     */
    void invalidate_ref_frames(int64_t first_frame, int64_t last_frame) override {
      logging::error("Encoder doesn't support reference frame invalidation");
      request_idr_frame();
    }

    avcodec_ctx_t avcodec_ctx;  ///< FFmpeg codec context owned by the encode session.
    std::unique_ptr<platf::avcodec_encode_device_t> device;  ///< Platform device used by the FFmpeg hardware encoder.

    std::vector<packet_raw_t::replace_t> replacements;  ///< NAL-unit byte ranges that must be replaced before packet send.

    cbs::nal_t sps;  ///< Original and rewritten sequence parameter set for IDR injection.
    cbs::nal_t vps;  ///< Original and rewritten HEVC video parameter set for IDR injection.

    // inject sps/vps data into idr pictures
    int inject;  ///< Number of upcoming IDR frames that should receive rewritten parameter sets.
  };

  /**
   * @brief NVENC encode session and device state for hardware encoding.
   */
  class nvenc_encode_session_t: public encode_session_t {
  public:
    /**
     * @brief Initialize an NVENC encode session and take ownership of its device.
     *
     * @param encode_device Encode device.
     */
    nvenc_encode_session_t(std::unique_ptr<platf::nvenc_encode_device_t> encode_device):
        device(std::move(encode_device)) {
    }

    /**
     * @brief Encode one frame with NVENC and return the packet payload.
     *
     * @param img Image or frame object to read from or populate.
     * @return Conversion status.
     */
    int convert(platf::img_t &img) override {
      if (!device) {
        return -1;
      }
      return device->convert(img);
    }

    /**
     * @brief Mark the frame as a request for an IDR frame.
     */
    void request_idr_frame() override {
      force_idr = true;
    }

    /**
     * @brief Mark the frame as a request for a normal inter frame.
     */
    void request_normal_frame() override {
      force_idr = false;
    }

    /**
     * @brief Mark the frame range whose references must be invalidated.
     *
     * @param first_frame First frame.
     * @param last_frame Last frame.
     */
    void invalidate_ref_frames(int64_t first_frame, int64_t last_frame) override {
      if (!device || !device->nvenc) {
        return;
      }

      if (!device->nvenc->invalidate_ref_frames(first_frame, last_frame)) {
        force_idr = true;
      }
    }

    /**
     * @brief Submit the next frame to NVENC and return the encoded payload.
     *
     * @param frame_index Monotonic frame index assigned by the video pipeline.
     * @return Encoded NVENC frame payload and frame metadata.
     */
    nvenc::nvenc_encoded_frame encode_frame(uint64_t frame_index) {
      if (!device || !device->nvenc) {
        return {};
      }

      auto result = device->nvenc->encode_frame(frame_index, force_idr);
      force_idr = false;
      return result;
    }

  private:
    std::unique_ptr<platf::nvenc_encode_device_t> device;
    bool force_idr = false;
  };

  /**
   * @brief Context object used while synchronizing encode sessions.
   */
  struct sync_session_ctx_t {
    safe::signal_t *join_event;  ///< Signal raised when the capture and encode workers should join.
    safe::mail_raw_t::event_t<bool> shutdown_event;  ///< Event raised when the stream should shut down.
    safe::mail_raw_t::queue_t<packet_t> packets;  ///< Queue receiving encoded video packets for the stream sender.
    safe::mail_raw_t::event_t<bool> idr_events;  ///< Event raised when an IDR frame is requested.
    safe::mail_raw_t::event_t<hdr_info_t> hdr_events;  ///< Event carrying updated HDR metadata.
    safe::mail_raw_t::event_t<input::touch_port_t> touch_port_events;  ///< Event carrying updated touch viewport metadata.

    config_t config;  ///< Stream or encoder configuration captured for the worker.
    int frame_nr;  ///< Next capture-frame number assigned to encoded packets.
    stream::session_t *channel_data;  ///< Borrowed transport session forwarded to packet senders; it must outlive the context and queued packets.
  };

  /**
   * @brief Synchronization state for one encode session.
   */
  struct sync_session_t {
    sync_session_ctx_t *ctx;  ///< Shared capture/encode synchronization context.
    std::unique_ptr<encode_session_t> session;  ///< Active encoder session used by the capture thread.
  };

  /**
   * @brief Queue of encode-session contexts waiting for capture work.
   */
  using encode_session_ctx_queue_t = safe::queue_t<sync_session_ctx_t>;
  /**
   * @brief Platform capture status returned by encode operations.
   */
  using encode_e = platf::capture_e;

  /**
   * @brief Capture thread context shared with the encoder session.
   */
  struct capture_ctx_t {
    img_event_t images;  ///< Queue of captured images waiting for encode.
    config_t config;  ///< Stream or encoder configuration captured for the worker.
  };

  /**
   * @brief Asynchronous capture thread state.
   */
  struct capture_thread_async_ctx_t {
    std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue;  ///< Capture ctx queue.
    std::jthread capture_thread;  ///< Capture thread.

    safe::signal_t reinit_event;  ///< Reinit event.
    const encoder_t *encoder_p;  ///< Encoder p.
    sync_util::sync_t<std::weak_ptr<platf::display_t>> display_wp;  ///< Display wp.
  };

  /**
   * @brief Synchronous capture thread state.
   */
  struct capture_thread_sync_ctx_t {
    encode_session_ctx_queue_t encode_session_ctx_queue {30};  ///< Encode session ctx queue.
  };

  /**
   * @brief Start the synchronous multi-client capture thread.
   *
   * @param host Host video service retaining the synchronous capture thread.
   * @return 0 when the capture thread is started.
   */
  int start_capture_sync(service_t::impl_t &host);
  /**
   * @brief Stop capture sync processing.
   *
   * @param ctx Native context object used by the operation or callback.
   */
  void end_capture_sync(capture_thread_sync_ctx_t &ctx);
  /**
   * @brief Start the asynchronous capture thread.
   *
   * @param host Host video service and its capture dependencies.
   * @param capture_thread_ctx Capture coordinator to initialize.
   * @return 0 when the capture thread is started; nonzero on setup failure.
   */
  int start_capture_async(service_t::impl_t &host, capture_thread_async_ctx_t &capture_thread_ctx);
  /**
   * @brief Stop capture async processing.
   *
   * @param ctx Native context object used by the operation or callback.
   */
  void end_capture_async(capture_thread_async_ctx_t &ctx);

  /**
   * @brief Copy immutable encoder descriptors for one service's probe state.
   * @param settings Immutable host encoder configuration.
   * @return Independent encoder descriptors with shared immutable format metadata.
   */
  std::vector<encoder_t> make_encoders(const config::settings_t &settings);

  /**
   * @brief Capture coordinators and encoder state owned by one video service.
   */
  struct service_t::impl_t {
    /**
     * @brief Bind capture dependencies and construct inactive coordinators.
     * @param settings Immutable encoder and capture configuration.
     * @param mail Host mailbox for encoded packets and display selection.
     * @param cursor Host cursor visibility preference.
     * @param displays Display configuration manager.
     * @param platform Native capture backend owner.
     */
    impl_t(const config::settings_t &settings, safe::mail_t mail, bool &cursor, display_device::manager_t &displays, platf::runtime_t &platform):
        settings {settings},
        mail {std::move(mail)},
        cursor {cursor},
        displays {displays},
        platform {platform},
        encoders {make_encoders(settings)},
        capture_thread_async {
          [this](capture_thread_async_ctx_t &ctx) {
            return start_capture_async(*this, ctx);
          },
          [](capture_thread_async_ctx_t &ctx) {
            end_capture_async(ctx);
          }
        },
        capture_thread_sync {
          [this](capture_thread_sync_ctx_t &) {
            return start_capture_sync(*this);
          },
          [](capture_thread_sync_ctx_t &ctx) {
            end_capture_sync(ctx);
          }
        } {
    }

    const config::settings_t &settings;  ///< Host startup encoder and capture configuration.
    safe::mail_t mail;  ///< Host packet and lifecycle mailbox.
    bool &cursor;  ///< Host cursor visibility preference.
    display_device::manager_t &displays;  ///< Display configuration dependency.
    platf::runtime_t &platform;  ///< Host capture backend owner.
    mutable std::mutex probe_mutex;  ///< Protects complete capability snapshots and probes.
    capabilities_t probed;  ///< Latest complete encoder probe result.
    std::vector<encoder_t> encoders;  ///< Encoder descriptors and capability flags owned by this host.
    encoder_t *chosen_encoder = nullptr;  ///< Encoder selected for the host.
    safe::shared_t<capture_thread_async_ctx_t> capture_thread_async;  ///< Shared asynchronous capture coordinator.
    safe::shared_t<capture_thread_sync_ctx_t> capture_thread_sync;  ///< Shared synchronous capture coordinator.
    std::jthread sync_thread;  ///< Synchronous capture worker joined before service destruction.
  };

#ifdef _WIN32
  /**
   * @brief Construct an NVENC descriptor bound to one host's immutable configuration.
   * @param settings Host startup configuration.
   * @return Encoder descriptor with independent probe capabilities.
   */
  encoder_t make_nvenc(const config::settings_t &settings) {
    return {
      "nvenc"sv,
      std::make_unique<encoder_platform_formats_nvenc>(
        platf::mem_type_e::dxgi,
        platf::pix_fmt_e::nv12,
        platf::pix_fmt_e::p010,
        platf::pix_fmt_e::ayuv,
        platf::pix_fmt_e::yuv444p16
      ),
      {
        {},  // Common options
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "av1_nvenc"s,
        {},  // capabilities
      },
      {
        {},  // Common options
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "hevc_nvenc"s,
        {},  // capabilities
      },
      {
        {},  // Common options
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "h264_nvenc"s,
        {},  // capabilities
      },
      PARALLEL_ENCODING | REF_FRAMES_INVALIDATION | YUV444_SUPPORT | ASYNC_TEARDOWN  // flags
    };
  }
#elif !defined(__APPLE__)
  /**
   * @brief Construct an NVENC descriptor bound to one host's immutable configuration.
   * @param settings Host startup configuration.
   * @return Encoder descriptor with independent probe capabilities.
   */
  encoder_t make_nvenc(const config::settings_t &settings) {
    return {
      "nvenc"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
  #ifdef _WIN32
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_D3D11,
  #else
        AV_HWDEVICE_TYPE_CUDA,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_CUDA,
  #endif
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV444P16,
  #ifdef _WIN32
        dxgi_init_avcodec_hardware_input_buffer
  #else
        cuda_init_avcodec_hardware_input_buffer
  #endif
      ),
      {
        // Common options
        {
          {"delay"s, 0},
          {"forced-idr"s, 1},
          {"zerolatency"s, 1},
          {"surfaces"s, 1},
          {"cbr_padding"s, false},
          {"preset"s, &settings.video.nv_legacy.preset},
          {"tune"s, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY},
          {"rc"s, NV_ENC_PARAMS_RC_CBR},
          {"multipass"s, &settings.video.nv_legacy.multipass},
          {"spatial-aq"s, &settings.video.nv_legacy.spatial_aq},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "av1_nvenc"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"delay"s, 0},
          {"forced-idr"s, 1},
          {"zerolatency"s, 1},
          {"surfaces"s, 1},
          {"cbr_padding"s, false},
          {"preset"s, &settings.video.nv_legacy.preset},
          {"tune"s, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY},
          {"rc"s, NV_ENC_PARAMS_RC_CBR},
          {"multipass"s, &settings.video.nv_legacy.multipass},
          {"spatial-aq"s, &settings.video.nv_legacy.spatial_aq},
        },
        {
          // SDR-specific options
          {"profile"s, std::to_underlying(nv::profile_hevc_e::main)},
        },
        {
          // HDR-specific options
          {"profile"s, std::to_underlying(nv::profile_hevc_e::main_10)},
        },
        // libavcodec will automatically use the rext profile for
        // YUV444 SDR and HDR content, so it doesn't need to be specified here
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "hevc_nvenc"s,
        {},  // capabilities
      },
      {
        {
          {"delay"s, 0},
          {"forced-idr"s, 1},
          {"zerolatency"s, 1},
          {"surfaces"s, 1},
          {"cbr_padding"s, false},
          {"preset"s, &settings.video.nv_legacy.preset},
          {"tune"s, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY},
          {"rc"s, NV_ENC_PARAMS_RC_CBR},
          {"coder"s, &settings.video.nv_legacy.h264_coder},
          {"multipass"s, &settings.video.nv_legacy.multipass},
          {"spatial-aq"s, &settings.video.nv_legacy.spatial_aq},
        },
        {
          // SDR-specific options
          {"profile"s, std::to_underlying(nv::profile_h264_e::high)},
        },
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "h264_nvenc"s,
        {},  // capabilities
      },
      PARALLEL_ENCODING | YUV444_SUPPORT
    };
  }
#endif

#ifdef _WIN32
  /**
   * @brief Quicksync.
   */
  encoder_t make_quicksync(const config::settings_t &settings) {
    return {
      "quicksync"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_QSV,
        AV_PIX_FMT_QSV,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_VUYX,
        AV_PIX_FMT_XV30,
        dxgi_init_avcodec_hardware_input_buffer
      ),
      {
        // Common options
        {
          {"preset"s, &settings.video.qsv.qsv_preset},
          {"forced_idr"s, 1},
          {"async_depth"s, 1},
          {"low_delay_brc"s, 1},
          {"low_power"s, 1},
        },
        {
          // SDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_av1_e::main)},
        },
        {
          // HDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_av1_e::main)},
        },
        {
          // YUV444 SDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_av1_e::high)},
        },
        {
          // YUV444 HDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_av1_e::high)},
        },
        {},  // Fallback options
        "av1_qsv"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"preset"s, &settings.video.qsv.qsv_preset},
          {"forced_idr"s, 1},
          {"async_depth"s, 1},
          {"low_delay_brc"s, 1},
          {"low_power"s, 1},
          {"recovery_point_sei"s, 0},
          {"pic_timing_sei"s, 0},
        },
        {
          // SDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_hevc_e::main)},
        },
        {
          // HDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_hevc_e::main_10)},
        },
        {
          // YUV444 SDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_hevc_e::rext)},
        },
        {
          // YUV444 HDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_hevc_e::rext)},
        },
        {
          // Fallback options
          {"low_power"s, [&settings]() {
             return settings.video.qsv.qsv_slow_hevc ? 0 : 1;
           }},
        },
        "hevc_qsv"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"preset"s, &settings.video.qsv.qsv_preset},
          {"cavlc"s, &settings.video.qsv.qsv_cavlc},
          {"forced_idr"s, 1},
          {"async_depth"s, 1},
          {"low_delay_brc"s, 1},
          {"low_power"s, 1},
          {"recovery_point_sei"s, 0},
          {"vcm"s, 1},
          {"pic_timing_sei"s, 0},
          {"max_dec_frame_buffering"s, 1},
        },
        {
          // SDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_h264_e::high)},
        },
        {},  // HDR-specific options
        {
          // YUV444 SDR-specific options
          {"profile"s, std::to_underlying(qsv::profile_h264_e::high_444p)},
        },
        {},  // YUV444 HDR-specific options
        {
          // Fallback options
          {"low_power"s, 0},  // Some old/low-end Intel GPUs don't support low power encoding
        },
        "h264_qsv"s,
        {},  // capabilities
      },
      PARALLEL_ENCODING | CBR_WITH_VBR | RELAXED_COMPLIANCE | NO_RC_BUF_LIMIT | YUV444_SUPPORT
    };
  }

  /**
   * @brief Amdvce.
   */
  encoder_t make_amdvce(const config::settings_t &settings) {
    return {
      "amdvce"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_D3D11,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_NONE,
        AV_PIX_FMT_NONE,
        dxgi_init_avcodec_hardware_input_buffer
      ),
      {
        // Common options
        {
          {"filler_data"s, false},
          {"forced_idr"s, 1},
          {"latency"s, "lowest_latency"s},
          {"async_depth"s, 1},
          {"skip_frame"s, 0},
          {"log_to_dbg"s, [&settings]() {
             return settings.sunshine.min_log_level < 2 ? 1 : 0;
           }},
          {"preencode"s, &settings.video.amd.amd_preanalysis},
          {"quality"s, &settings.video.amd.amd_quality_av1},
          {"rc"s, &settings.video.amd.amd_rc_av1},
          {"usage"s, &settings.video.amd.amd_usage_av1},
          {"enforce_hrd"s, &settings.video.amd.amd_enforce_hrd},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "av1_amf"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"filler_data"s, false},
          {"forced_idr"s, 1},
          {"latency"s, 1},
          {"async_depth"s, 1},
          {"skip_frame"s, 0},
          {"log_to_dbg"s, [&settings]() {
             return settings.sunshine.min_log_level < 2 ? 1 : 0;
           }},
          {"gops_per_idr"s, 1},
          {"header_insertion_mode"s, "idr"s},
          {"preencode"s, &settings.video.amd.amd_preanalysis},
          {"quality"s, &settings.video.amd.amd_quality_hevc},
          {"rc"s, &settings.video.amd.amd_rc_hevc},
          {"usage"s, &settings.video.amd.amd_usage_hevc},
          {"vbaq"s, &settings.video.amd.amd_vbaq},
          {"enforce_hrd"s, &settings.video.amd.amd_enforce_hrd},
          {"max_au_size"s, &settings.video.amd.amd_max_au_size},
          {"level"s, [](const config_t &cfg) {
             auto size = cfg.width * cfg.height;
             // For 4K and below, try to use level 5.1 or 5.2 if possible
             if (size <= 8912896) {
               if (size * cfg.framerate <= 534773760) {
                 return "5.1"s;
               } else if (size * cfg.framerate <= 1069547520) {
                 return "5.2"s;
               }
             }
             return "auto"s;
           }},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "hevc_amf"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"filler_data"s, false},
          {"forced_idr"s, 1},
          {"latency"s, 1},
          {"async_depth"s, 1},
          {"frame_skipping"s, 0},
          {"log_to_dbg"s, [&settings]() {
             return settings.sunshine.min_log_level < 2 ? 1 : 0;
           }},
          {"preencode"s, &settings.video.amd.amd_preanalysis},
          {"quality"s, &settings.video.amd.amd_quality_h264},
          {"rc"s, &settings.video.amd.amd_rc_h264},
          {"usage"s, &settings.video.amd.amd_usage_h264},
          {"vbaq"s, &settings.video.amd.amd_vbaq},
          {"coder"s, &settings.video.amd.amd_coder},
          {"enforce_hrd"s, &settings.video.amd.amd_enforce_hrd},
          {"max_au_size"s, &settings.video.amd.amd_max_au_size},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {
          // Fallback options
          {"usage"s, 2 /* AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY */},  // Workaround for https://github.com/GPUOpen-LibrariesAndSDKs/AMF/issues/410
        },
        "h264_amf"s,
        {},  // capabilities
      },
      PARALLEL_ENCODING
    };
  }

  /**
   * @brief Mediafoundation.
   */
  encoder_t make_mediafoundation(const config::settings_t &settings) {
    return {
      "mediafoundation"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_D3D11,
        AV_PIX_FMT_NV12,  // SDR 4:2:0 8-bit (only format Qualcomm supports)
        AV_PIX_FMT_NONE,  // No HDR - Qualcomm MF only supports 8-bit
        AV_PIX_FMT_NONE,  // No YUV444 SDR
        AV_PIX_FMT_NONE,  // No YUV444 HDR
        dxgi_init_avcodec_hardware_input_buffer
      ),
      {
        // Common options for AV1 - Qualcomm MF encoder
        {
          {"hw_encoding"s, 1},
          {"rate_control"s, "cbr"s},
          {"scenario"s, "display_remoting"s},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "av1_mf"s,
        {},  // capabilities
      },
      {
        // Common options for HEVC - Qualcomm MF encoder
        {
          {"hw_encoding"s, 1},
          {"rate_control"s, "cbr"s},
          {"scenario"s, "display_remoting"s},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "hevc_mf"s,
        {},  // capabilities
      },
      {
        // Common options for H.264 - Qualcomm MF encoder
        {
          {"hw_encoding"s, 1},
          {"rate_control"s, "cbr"s},
          {"scenario"s, "display_remoting"s},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "h264_mf"s,
        {},  // capabilities
      },
      PARALLEL_ENCODING | FIXED_GOP_SIZE  // MF encoder doesn't support on-demand IDR frames
    };
  }
#endif

  /**
   * @brief Software.
   */
  encoder_t make_software(const config::settings_t &settings) {
    return {
      "software"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
        AV_HWDEVICE_TYPE_NONE,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_NONE,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV420P10,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV444P10,
        nullptr
      ),
      {
        // libsvtav1 takes different presets than libx264/libx265.
        // We set an infinite GOP length, use a low delay prediction structure,
        // force I frames to be key frames, and set max bitrate to default to work
        // around a FFmpeg bug with CBR mode.
        {
          {"svtav1-params"s, "keyint=-1:pred-struct=1:force-key-frames=1:mbr=0"s},
          {"preset"s, &settings.video.sw.svtav1_preset},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options

#ifdef ENABLE_BROKEN_AV1_ENCODER
             // Due to bugs preventing on-demand IDR frames from working and very poor
             // real-time encoding performance, we do not enable libsvtav1 by default.
             // It is only suitable for testing AV1 until the IDR frame issue is fixed.
        "libsvtav1"s,
#else
        {},
#endif
        {},  // capabilities
      },
      {
        // x265's Info SEI is so long that it causes the IDR picture data to be
        // kicked to the 2nd packet in the frame, breaking Moonlight's parsing logic.
        // It also looks like gop_size isn't passed on to x265, so we have to set
        // 'keyint=-1' in the parameters ourselves.
        {
          {"forced-idr"s, 1},
          {"x265-params"s, "info=0:keyint=-1"s},
          {"preset"s, &settings.video.sw.sw_preset},
          {"tune"s, &settings.video.sw.sw_tune},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "libx265"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"preset"s, &settings.video.sw.sw_preset},
          {"tune"s, &settings.video.sw.sw_tune},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "libx264"s,
        {},  // capabilities
      },
      H264_ONLY | PARALLEL_ENCODING | ALWAYS_REPROBE | YUV444_SUPPORT
    };
  }

#if defined(__linux__) || defined(linux) || defined(__linux) || defined(__FreeBSD__)
  /**
   * @brief VA-API.
   */
  encoder_t make_vaapi(const config::settings_t &settings) {
    return {
      "vaapi"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
        AV_HWDEVICE_TYPE_VAAPI,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_NONE,
        AV_PIX_FMT_NONE,
        [&settings](platf::avcodec_encode_device_t *device) {
          return vaapi_init_avcodec_hardware_input_buffer(settings, device);
        }
      ),
      {
        // Common options
        {
          {"async_depth"s, 1},
          {"blbrc"s, &settings.video.vaapi.blbrc},
          {"idr_interval"s, std::numeric_limits<int>::max()},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "av1_vaapi"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"async_depth"s, 1},
          {"blbrc"s, &settings.video.vaapi.blbrc},
          {"sei"s, 0},
          {"idr_interval"s, std::numeric_limits<int>::max()},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "hevc_vaapi"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"async_depth"s, 1},
          {"blbrc"s, &settings.video.vaapi.blbrc},
          {"sei"s, 0},
          {"idr_interval"s, std::numeric_limits<int>::max()},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "h264_vaapi"s,
        {},  // capabilities
      },
      // RC buffer size will be set in platform code if supported
      LIMITED_GOP_SIZE | PARALLEL_ENCODING | NO_RC_BUF_LIMIT
    };
  }

  #ifdef SUNSHINE_BUILD_VULKAN
  encoder_t make_vulkan(const config::settings_t &settings) {
    return {
      "vulkan"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
        AV_HWDEVICE_TYPE_VULKAN,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_VULKAN,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_NONE,
        AV_PIX_FMT_NONE,
        [&settings](platf::avcodec_encode_device_t *device) {
          return vulkan_init_avcodec_hardware_input_buffer(settings, device);
        }
      ),
      {
        // AV1
        {
          {"idr_interval"s, std::numeric_limits<int>::max()},
          {"tune"s, &settings.video.vk.tune},
          {"rc_mode"s, &settings.video.vk.rc_mode},
          {"units"s, 0},
          {"usage"s, "stream"s},
          {"content"s, "rendered"s},
          {"async_depth"s, 1},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "av1_vulkan"s,
        {},  // capabilities
      },
      {
        // HEVC
        {
          {"idr_interval"s, std::numeric_limits<int>::max()},
          {"tune"s, &settings.video.vk.tune},
          {"rc_mode"s, &settings.video.vk.rc_mode},
          {"units"s, 0},
          {"usage"s, "stream"s},
          {"content"s, "rendered"s},
          {"async_depth"s, 1},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "hevc_vulkan"s,
        {},  // capabilities
      },
      {
        // H.264
        {
          {"idr_interval"s, std::numeric_limits<int>::max()},
          {"tune"s, &settings.video.vk.tune},
          {"rc_mode"s, &settings.video.vk.rc_mode},
          {"units"s, 0},
          {"usage"s, "stream"s},
          {"content"s, "rendered"s},
          {"async_depth"s, 1},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "h264_vulkan"s,
        {},  // capabilities
      },
      LIMITED_GOP_SIZE | PARALLEL_ENCODING
    };
  }
  #endif  // SUNSHINE_BUILD_VULKAN
#endif  // linux

#ifdef __APPLE__
  /**
   * @brief Videotoolbox.
   */
  encoder_t make_videotoolbox(const config::settings_t &settings) {
    return {
      "videotoolbox"sv,
      std::make_unique<encoder_platform_formats_avcodec>(
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
        AV_HWDEVICE_TYPE_NONE,
        AV_PIX_FMT_VIDEOTOOLBOX,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_NONE,
        AV_PIX_FMT_NONE,
        vt_init_avcodec_hardware_input_buffer
      ),
      {
        // Common options
        {
          {"allow_sw"s, &settings.video.vt.vt_allow_sw},
          {"require_sw"s, &settings.video.vt.vt_require_sw},
          {"realtime"s, &settings.video.vt.vt_realtime},
          {"prio_speed"s, 1},
          {"max_ref_frames"s, 1},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "av1_videotoolbox"s,
        {},  // capabilities
      },
      {
        // Common options
        {
          {"allow_sw"s, &settings.video.vt.vt_allow_sw},
          {"require_sw"s, &settings.video.vt.vt_require_sw},
          {"realtime"s, &settings.video.vt.vt_realtime},
          {"prio_speed"s, 1},
          {"max_ref_frames"s, 1},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {},  // Fallback options
        "hevc_videotoolbox"s,
        {},  // capabilities
      },
      {
        // Common options
        // Note: max_ref_frames is intentionally omitted for H.264 because
        // VideoToolbox on Apple Silicon produces all-IDR output when
        // ReferenceBufferCount=1 is set for H.264, causing massive bandwidth
        // inflation (~3x) and frame drops. HEVC and AV1 are unaffected and
        // retain max_ref_frames=1. See LizardByte/Sunshine#5013.
        {
          {"allow_sw"s, &settings.video.vt.vt_allow_sw},
          {"require_sw"s, &settings.video.vt.vt_require_sw},
          {"realtime"s, &settings.video.vt.vt_realtime},
          {"prio_speed"s, 1},
        },
        {},  // SDR-specific options
        {},  // HDR-specific options
        {},  // YUV444 SDR-specific options
        {},  // YUV444 HDR-specific options
        {
          // Fallback options
          {"flags"s, "-low_delay"},
        },
        "h264_videotoolbox"s,
        {},  // capabilities
      },
      PARALLEL_ENCODING
    };
  }
#endif

  std::vector<encoder_t> make_encoders(const config::settings_t &settings) {
    return {
#ifndef __APPLE__
      make_nvenc(settings),
#endif
#ifdef _WIN32
      make_quicksync(settings),
      make_amdvce(settings),
      make_mediafoundation(settings),
#endif
#if defined(__linux__) || defined(linux) || defined(__linux) || defined(__FreeBSD__)
  #ifdef SUNSHINE_BUILD_VULKAN
      make_vulkan(settings),
  #endif
      make_vaapi(settings),
#endif
#ifdef __APPLE__
      make_videotoolbox(settings),
#endif
      make_software(settings)
    };
  }

  /**
   * @brief Recreate a display capture object after a capture failure.
   *
   * @param disp Display connection or display handle.
   * @param type Protocol, message, or resource type selector.
   * @param display_name Display name.
   * @param config Configuration values to apply.
   * @param host Host input or video service and its capture dependencies.
   */
  void reset_display(service_t::impl_t &host, std::shared_ptr<platf::display_t> &disp, const platf::mem_type_e &type, const std::string &display_name, const config_t &config) {
    // We try this twice, in case we still get an error on reinitialization
    for (int x = 0; x < 2; ++x) {
      disp.reset();
      disp = host.platform.display(type, display_name, config);
      if (disp) {
        break;
      }

      // The capture code depends on us to sleep between failures
      std::this_thread::sleep_for(200ms);
    }
  }

  /**
   * @brief Update the list of display names before or during a stream.
   * @details This will attempt to keep `current_display_index` pointing at the same display.
   * @param dev_type The encoder device type used for display lookup.
   * @param display_names The list of display names to repopulate.
   * @param current_display_index The current display index or -1 if not yet known.
   * @param host Host input or video service and its capture dependencies.
   */
  void refresh_displays(service_t::impl_t &host, platf::mem_type_e dev_type, std::vector<std::string> &display_names, int &current_display_index) {
    // It is possible that the output name may be empty even if it wasn't before (device disconnected) or vice-versa
    const auto output_name {host.displays.map_output_name(host.settings.video.output_name)};
    std::string current_display_name;

    // If we have a current display index, let's start with that
    if (current_display_index >= 0 && current_display_index < display_names.size()) {
      current_display_name = display_names.at(current_display_index);
    }

    // Refresh the display names
    auto old_display_names = std::move(display_names);
    display_names = host.platform.display_names(dev_type);

    // If we now have no displays, let's put the old display array back and fail
    if (display_names.empty() && !old_display_names.empty()) {
      logging::error("No displays were found after reenumeration!"sv);
      display_names = std::move(old_display_names);
      return;
    } else if (display_names.empty()) {
      display_names.emplace_back(output_name);
    }

    // We now have a new display name list, so reset the index back to 0
    current_display_index = 0;

    // If we had a name previously, let's try to find it in the new list
    if (!current_display_name.empty()) {
      for (int x = 0; x < display_names.size(); ++x) {
        if (display_names[x] == current_display_name) {
          current_display_index = x;
          return;
        }
      }

      // The old display was removed, so we'll start back at the first display again
      logging::warning("Previous active display [{}] is no longer present", current_display_name);
    } else {
      for (int x = 0; x < display_names.size(); ++x) {
        if (display_names[x] == output_name) {
          current_display_index = x;
          return;
        }
      }
    }
  }

  /**
   * @brief Remove stopped capture subscribers and deliver a captured frame to the remaining sessions.
   * @param contexts Active capture subscribers updated in capture-thread order.
   * @param image Captured image retained by each live subscriber.
   * @param captured Whether the callback carries a new image.
   */
  void distribute_captured_image(std::vector<capture_ctx_t> &contexts, const std::shared_ptr<platf::img_t> &image, bool captured) {
    for (auto context = contexts.begin(); context != contexts.end();) {
      if (!context->images->running()) {
        context = contexts.erase(context);
        continue;
      }
      if (captured) {
        context->images->raise(image);
      }
      ++context;
    }
  }

  /**
   * @brief Drain queued image references before destroying the active display.
   * @param contexts Subscribers whose unused images may retain the display.
   */
  void drain_capture_images(std::vector<capture_ctx_t> &contexts) {
    for (auto context = contexts.begin(); context != contexts.end();) {
      if (!context->images->running()) {
        context = contexts.erase(context);
        continue;
      }
      while (context->images->try_pop()) {}
      ++context;
    }
  }

  /**
   * @brief Borrow the display names, selected index and switch event for one capture run.
   */
  struct capture_display_selection_t {
    std::vector<std::string> &names;  ///< Display names refreshed during reopening.
    int &index;  ///< Selected display index updated by pending switch requests.
    const safe::mail_raw_t::event_t<int> &switches;  ///< Retained switch-event handle borrowed from the capture run.
  };

  /**
   * @brief Borrow the resources released and republished during asynchronous display reinitialization.
   */
  struct capture_reinitialization_t {
    std::shared_ptr<platf::display_t> &display;  ///< Active display owner replaced after its old users release it.
    sync_util::sync_t<std::weak_ptr<platf::display_t>> &display_wp;  ///< Weak display published to encoder threads.
    detail::capture_image_pool_t &images;  ///< Image pool cleared before waiting for display release.
    safe::signal_t &reinit;  ///< Signal kept raised until the replacement display is published.
  };

  /**
   * @brief Refresh display selection and retry opening it while the capture queue is active.
   * @tparam Queue Capture-session queue providing its running state.
   * @param host Host platform and display dependencies.
   * @param display Display released before each reenumeration attempt.
   * @param type Capture memory type required by the encoder.
   * @param selection Borrowed display selection updated during each reopening attempt.
   * @param config First active session's capture configuration.
   * @param queue Queue controlling retry lifetime.
   * @return Whether a display was opened before the queue stopped.
   */
  template<typename Queue>
  bool open_capture_display(service_t::impl_t &host, std::shared_ptr<platf::display_t> &display, platf::mem_type_e type, const capture_display_selection_t &selection, const config_t &config, const Queue &queue) {
    auto &names = selection.names;
    auto &index = selection.index;
    const auto &switches = selection.switches;
    display.reset();
    while (queue.running()) {
      display.reset();
      refresh_displays(host, type, names, index);
      if (switches->peek()) {
        index = std::clamp(*switches->pop(), 0, static_cast<int>(names.size()) - 1);
      }
      reset_display(host, display, type, names[index], config);
      if (display) {
        break;
      }
    }
    return static_cast<bool>(display);
  }

  /**
   * @brief Release display-bound images and wait for encoders before reopening asynchronous capture.
   * @param host Host platform and display dependencies.
   * @param capture Borrowed capture resources released and republished in reinitialization order.
   * @param contexts Capture subscribers drained during the wait.
   * @param queue Session queue controlling reopening lifetime.
   * @param encoder Selected encoder's immutable capture formats.
   * @param selection Borrowed display names, current index and switch event.
   * @return Whether capture can resume with a replacement display.
   */
  bool reinitialize_capture_display(service_t::impl_t &host, const capture_reinitialization_t &capture, std::vector<capture_ctx_t> &contexts, const std::shared_ptr<safe::queue_t<capture_ctx_t>> &queue, const encoder_t &encoder, const capture_display_selection_t &selection) {
    auto &display = capture.display;
    auto &display_wp = capture.display_wp;
    auto &images = capture.images;
    auto &reinit = capture.reinit;
    reinit.raise(true);
    images.clear();
    while (display_wp->use_count() != 1) {
      drain_capture_images(contexts);
      std::this_thread::sleep_for(20ms);
    }
    if (!open_capture_display(host, display, encoder.platform_formats->dev_type, selection, contexts.front().config, *queue)) {
      return false;
    }
    display_wp = display;
    reinit.reset();
    return true;
  }

  /**
   * @brief Stop image delivery for active and queued asynchronous capture subscribers.
   * @param contexts Subscribers already attached to the capture run.
   * @param queue Pending subscribers stopped before inspecting their retained storage.
   */
  void stop_capture_subscribers(std::vector<capture_ctx_t> &contexts, const std::shared_ptr<safe::queue_t<capture_ctx_t>> &queue) {
    queue->stop();
    for (auto &context : contexts) {
      context.images->stop();
    }
    for (auto &context : queue->unsafe()) {
      context.images->stop();
    }
  }

  /**
   * @brief Deliver a capture tick before attaching new subscribers and handling display switches.
   * @param contexts Active subscribers pruned and notified before new subscribers are attached.
   * @param queue Pending subscriber queue controlling capture lifetime.
   * @param image Captured image retained only when a new frame is available.
   * @param captured Whether this tick carries a new frame.
   * @param switches Retained display-switch event.
   * @param artificial_reinit Set when capture should stop for a requested display switch.
   * @return Whether the native capture loop should continue.
   */
  bool deliver_capture_tick(std::vector<capture_ctx_t> &contexts, const std::shared_ptr<safe::queue_t<capture_ctx_t>> &queue, const std::shared_ptr<platf::img_t> &image, bool captured, const safe::mail_raw_t::event_t<int> &switches, bool &artificial_reinit) {
    distribute_captured_image(contexts, image, captured);
    if (!queue->running()) {
      return false;
    }
    while (queue->peek()) {
      contexts.emplace_back(std::move(*queue->pop()));
    }
    if (switches->peek()) {
      artificial_reinit = true;
      return false;
    }
    return true;
  }

  /**
   * @brief Run the shared display capture thread for asynchronous encoding.
   *
   * @param capture_ctx_queue Capture context queue.
   * @param display_wp Weak pointer holder for the active display.
   * @param reinit_event Signal raised while the display is being reinitialized.
   * @param encoder Selected encoder.
   * @param host Host input or video service and its capture dependencies.
   */
  void captureThread(service_t::impl_t &host, std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue, sync_util::sync_t<std::weak_ptr<platf::display_t>> &display_wp, safe::signal_t &reinit_event, const encoder_t &encoder) {
    std::vector<capture_ctx_t> capture_ctxs;

    auto fg = util::fail_guard([&]() {
      stop_capture_subscribers(capture_ctxs, capture_ctx_queue);
    });

    auto switch_display_event = host.mail->event<int>(mail::switch_display);

    // Wait for the initial capture context or a request to stop the queue
    auto initial_capture_ctx = capture_ctx_queue->pop();
    if (!initial_capture_ctx) {
      return;
    }
    capture_ctxs.emplace_back(std::move(*initial_capture_ctx));

    // Get all the monitor names now, rather than at boot, to
    // get the most up-to-date list available monitors
    std::vector<std::string> display_names;
    int display_p = -1;
    refresh_displays(host, encoder.platform_formats->dev_type, display_names, display_p);
    auto disp = host.platform.display(encoder.platform_formats->dev_type, display_names[display_p], capture_ctxs.front().config);
    if (!disp) {
      return;
    }
    display_wp = disp;

    detail::capture_image_pool_t images;
    auto pull_free_image_callback = [&images, &disp, &capture_ctx_queue](std::shared_ptr<platf::img_t> &img_out) -> bool {
      img_out.reset();
      while (capture_ctx_queue->running()) {
        img_out = images.acquire([&disp] {
          return disp->alloc_img();
        },
                                 [] {
                                   return std::chrono::steady_clock::now();
                                 });
        if (img_out) {
          return true;
        }
        std::this_thread::sleep_for(1ms);
      }
      return false;
    };

    // Capture takes place on this thread
    platf::set_thread_name("video::capture");
    platf::adjust_thread_priority(platf::thread_priority_e::critical);

    while (capture_ctx_queue->running()) {
      bool artificial_reinit = false;

      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        return deliver_capture_tick(capture_ctxs, capture_ctx_queue, img, frame_captured, switch_display_event, artificial_reinit);
      };

      auto status = disp->capture(push_captured_image_callback, pull_free_image_callback, &host.cursor);

      if (artificial_reinit && status != platf::capture_e::error) {
        status = platf::capture_e::reinit;

        artificial_reinit = false;
      }

      switch (status) {
        case platf::capture_e::reinit:
          {
            if (!reinitialize_capture_display(host, {disp, display_wp, images, reinit_event}, capture_ctxs, capture_ctx_queue, encoder, {display_names, display_p, switch_display_event})) {
              return;
            }
            continue;
          }
        case platf::capture_e::error:
        case platf::capture_e::ok:
        case platf::capture_e::timeout:
        case platf::capture_e::interrupted:
          return;
        default:
          logging::error("Unrecognized capture status [{}]", std::to_underlying(status));
          return;
      }
    }
  }

  /**
   * @brief Drain encoded packets from an FFmpeg encoder session.
   *
   * @param frame_nr Monotonic frame index assigned by the video pipeline.
   * @param session Active FFmpeg encoder session.
   * @param packets Shared handle to the output queue that receives encoded packets.
   * @param channel_data Borrowed transport session that must outlive consumption of the queued packets; null for encoder probes.
   * @param frame_timestamp Capture timestamp associated with the encoded frame.
   * @return 0 when packets are queued; nonzero when encoding or packetization fails.
   */
  int encode_avcodec(int64_t frame_nr, avcodec_encode_session_t &session, const safe::mail_raw_t::queue_t<packet_t> &packets, stream::session_t *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp) {
    auto &frame = session.device->frame;
    frame->pts = frame_nr;

    auto &ctx = session.avcodec_ctx;

    auto &sps = session.sps;
    auto &vps = session.vps;

    // send the frame to the encoder
    auto ret = avcodec_send_frame(ctx.get(), frame);
    if (ret < 0) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> err_str {0};
      logging::error([&err_str, &ret](std::ostream &out) {
        out << "Could not send a frame for encoding: "sv << av_make_error_string(err_str.data(), AV_ERROR_MAX_STRING_SIZE, ret);
      });

      return -1;
    }

    while (ret >= 0) {
      auto packet = std::make_unique<packet_raw_avcodec>();
      auto av_packet = packet.get()->av_packet;

      ret = avcodec_receive_packet(ctx.get(), av_packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
      } else if (ret < 0) {
        return ret;
      }

      if (av_packet->flags & AV_PKT_FLAG_KEY) {
        logging::debug("Frame {}: IDR Keyframe (AV_FRAME_FLAG_KEY)", frame_nr);
      }

      if ((frame->flags & AV_FRAME_FLAG_KEY) && !(av_packet->flags & AV_PKT_FLAG_KEY)) {
        logging::error("Encoder did not produce IDR frame when requested!"sv);
      }

      if (session.inject) {
        if (session.inject == 1) {
          auto h264 = cbs::make_sps_h264(ctx.get(), av_packet);

          sps = std::move(h264.sps);
        } else {
          auto hevc = cbs::make_sps_hevc(ctx.get(), av_packet);

          sps = std::move(hevc.sps);
          vps = std::move(hevc.vps);

          session.replacements.emplace_back(
            std::string_view(reinterpret_cast<const char *>(std::begin(vps.old)), vps.old.size()),
            std::string_view(reinterpret_cast<const char *>(std::begin(vps._new)), vps._new.size())
          );
        }

        session.inject = 0;

        session.replacements.emplace_back(
          std::string_view(reinterpret_cast<const char *>(std::begin(sps.old)), sps.old.size()),
          std::string_view(reinterpret_cast<const char *>(std::begin(sps._new)), sps._new.size())
        );
      }

      if (av_packet && av_packet->pts == frame_nr) {
        packet->frame_timestamp = frame_timestamp;
      }

      packet->replacements = &session.replacements;
      packet->channel_data = channel_data;
      packets->raise(std::move(packet));
    }

    return 0;
  }

  /**
   * @brief Encode one frame through NVENC and queue the resulting packet.
   *
   * @param frame_nr Monotonic frame index assigned by the video pipeline.
   * @param session Active NVENC encoder session.
   * @param packets Shared handle to the output queue that receives the encoded packet.
   * @param channel_data Borrowed transport session that must outlive consumption of the queued packet; null for encoder probes.
   * @param frame_timestamp Capture timestamp associated with the encoded frame.
   * @return 0 when packets are queued; nonzero when NVENC encoding fails.
   */
  int encode_nvenc(int64_t frame_nr, nvenc_encode_session_t &session, const safe::mail_raw_t::queue_t<packet_t> &packets, stream::session_t *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp) {
    auto encoded_frame = session.encode_frame(frame_nr);
    if (encoded_frame.data.empty()) {
      logging::error("NvENC returned empty packet");
      return -1;
    }

    if (frame_nr != encoded_frame.frame_index) {
      logging::error("NvENC frame index mismatch {} {}", frame_nr, encoded_frame.frame_index);
    }

    auto packet = std::make_unique<packet_raw_generic>(std::move(encoded_frame.data), encoded_frame.frame_index, encoded_frame.idr);
    packet->channel_data = channel_data;
    packet->after_ref_frame_invalidation = encoded_frame.after_ref_frame_invalidation;
    packet->frame_timestamp = frame_timestamp;
    packets->raise(std::move(packet));

    return 0;
  }

  /**
   * @brief Encode one captured frame and queue packets for transmission.
   *
   * @param frame_nr Frame nr.
   * @param session Active streaming or pairing session for the request.
   * @param packets Shared handle to the queue that receives encoded packets.
   * @param channel_data Borrowed transport session that must outlive consumption of the queued packets; null for encoder probes.
   * @param frame_timestamp Frame timestamp.
   * @return 0 when the frame is encoded and queued; nonzero on encoder failure.
   */
  int encode(int64_t frame_nr, encode_session_t &session, const safe::mail_raw_t::queue_t<packet_t> &packets, stream::session_t *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp) {
    if (auto avcodec_session = dynamic_cast<avcodec_encode_session_t *>(&session)) {
      return encode_avcodec(frame_nr, *avcodec_session, packets, channel_data, frame_timestamp);
    } else if (auto nvenc_session = dynamic_cast<nvenc_encode_session_t *>(&session)) {
      return encode_nvenc(frame_nr, *nvenc_session, packets, channel_data, frame_timestamp);
    }

    return -1;
  }

  /**
   * @brief Create an AVCodec encode session.
   *
   * @param settings Host encoder settings used by the session.
   * @param disp Display being encoded.
   * @param encoder Selected encoder.
   * @param config Video configuration.
   * @param width Encoded frame width.
   * @param height Encoded frame height.
   * @param encode_device AVCodec encode device.
   * @return AVCodec encode session, or nullptr on failure.
   */
  std::unique_ptr<avcodec_encode_session_t> make_avcodec_encode_session(
    const config::settings_t &settings,
    platf::display_t *disp,
    const encoder_t &encoder,
    const config_t &config,
    int width,
    int height,
    std::unique_ptr<platf::avcodec_encode_device_t> encode_device
  ) {
    auto platform_formats = dynamic_cast<const encoder_platform_formats_avcodec *>(encoder.platform_formats.get());
    if (!platform_formats) {
      return nullptr;
    }

    bool hardware = platform_formats->avcodec_base_dev_type != AV_HWDEVICE_TYPE_NONE;

    auto &video_format = encoder.codec_from_config(config);
    if (!video_format[encoder_t::PASSED] || !disp->is_codec_supported(video_format.name, config)) {
      logging::error("{}: {} mode not supported", encoder.name, video_format.name);
      return nullptr;
    }

    if (config.chromaSamplingType == 1) {
      if (!video_format[encoder_t::YUV444]) {
        logging::error("{}: YUV 4:4:4 not supported", video_format.name);
        return nullptr;
      }

      if (config.dynamicRange && !video_format[encoder_t::DYNAMIC_RANGE_YUV444]) {
        logging::error("{}: YUV 4:4:4 dynamic range not supported", video_format.name);
        return nullptr;
      }

    } else {
      if (config.dynamicRange && !video_format[encoder_t::DYNAMIC_RANGE]) {
        logging::error("{}: dynamic range not supported", video_format.name);
        return nullptr;
      }
    }

    auto codec = avcodec_find_encoder_by_name(video_format.name.c_str());
    if (!codec) {
      logging::error("Couldn't open [{}]", video_format.name);

      return nullptr;
    }

    auto colorspace = encode_device->colorspace;
    auto sw_fmt = (colorspace.bit_depth == 8 && config.chromaSamplingType == 0)  ? platform_formats->avcodec_pix_fmt_8bit :
                  (colorspace.bit_depth == 8 && config.chromaSamplingType == 1)  ? platform_formats->avcodec_pix_fmt_yuv444_8bit :
                  (colorspace.bit_depth == 10 && config.chromaSamplingType == 0) ? platform_formats->avcodec_pix_fmt_10bit :
                  (colorspace.bit_depth == 10 && config.chromaSamplingType == 1) ? platform_formats->avcodec_pix_fmt_yuv444_10bit :
                                                                                   AV_PIX_FMT_NONE;

    // Allow up to 1 retry to apply the set of fallback options.
    //
    // Note: If we later end up needing multiple sets of
    // fallback options, we may need to allow more retries
    // to try applying each set.
    avcodec_ctx_t ctx;
    for (int retries = 0; retries < 2; retries++) {
      ctx.reset(avcodec_alloc_context3(codec));
      ctx->width = config.width;
      ctx->height = config.height;
      const AVRational fps = video::framerate_to_rational(config);
      ctx->framerate = fps;
      ctx->time_base = AVRational {fps.den, fps.num};

      switch (config.videoFormat) {
        case 0:
          // 10-bit h264 encoding is not supported by our streaming protocol
          assert(!config.dynamicRange);
          ctx->profile = select_h264_profile(video_format.name, config, settings.video.amd.amd_coder);
          break;

        case 1:
          if (config.chromaSamplingType == 1) {
            // HEVC uses the same RExt profile for both 8 and 10 bit YUV 4:4:4 encoding
            ctx->profile = AV_PROFILE_HEVC_REXT;
          } else {
            ctx->profile = config.dynamicRange ? AV_PROFILE_HEVC_MAIN_10 : AV_PROFILE_HEVC_MAIN;
          }
          break;

        case 2:
          // AV1 supports both 8 and 10 bit encoding with the same Main profile
          // but YUV 4:4:4 sampling requires High profile
          ctx->profile = (config.chromaSamplingType == 1) ? AV_PROFILE_AV1_HIGH : AV_PROFILE_AV1_MAIN;
          break;
      }

      // B-frames delay decoder output, so never use them
      ctx->max_b_frames = 0;

      // Use an infinite GOP length since I-frames are generated on demand
      // Exception: encoders with FIXED_GOP_SIZE flag don't support on-demand IDR
      if (encoder.flags & FIXED_GOP_SIZE) {
        // Fixed GOP for encoders that don't support on-demand IDR (e.g. Media Foundation)
        ctx->gop_size = 120;  // ~2 seconds at 60 FPS - larger to reduce oversized IDR frame frequency
        ctx->keyint_min = 120;
      } else {
        ctx->gop_size = encoder.flags & LIMITED_GOP_SIZE ?
                          std::numeric_limits<std::int16_t>::max() :
                          std::numeric_limits<int>::max();
        ctx->keyint_min = std::numeric_limits<int>::max();
      }

      // Some client decoders have limits on the number of reference frames
      if (config.numRefFrames) {
        if (video_format[encoder_t::REF_FRAMES_RESTRICT]) {
          ctx->refs = config.numRefFrames;
        } else {
          logging::warning("Client requested reference frame limit, but encoder doesn't support it!"sv);
        }
      }

      // We forcefully reset the flags to avoid clash on reuse of AVCodecContext
      ctx->flags = 0;
      ctx->flags |= AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY;

      ctx->flags2 |= AV_CODEC_FLAG2_FAST;

      auto avcodec_colorspace = avcodec_colorspace_from_sunshine_colorspace(colorspace);

      ctx->color_range = avcodec_colorspace.range;
      ctx->color_primaries = avcodec_colorspace.primaries;
      ctx->color_trc = avcodec_colorspace.transfer_function;
      ctx->colorspace = avcodec_colorspace.matrix;

      // Used by cbs::make_sps_hevc
      ctx->sw_pix_fmt = sw_fmt;

      if (hardware) {
        avcodec_buffer_t encoding_stream_context;

        ctx->pix_fmt = platform_formats->avcodec_dev_pix_fmt;

        // Create the base hwdevice context
        auto buf_or_error = platform_formats->init_avcodec_hardware_input_buffer(encode_device.get());
        if (buf_or_error.has_right()) {
          return nullptr;
        }
        encoding_stream_context = std::move(buf_or_error.left());

        // If this encoder requires derivation from the base, derive the desired type
        if (platform_formats->avcodec_derived_dev_type != AV_HWDEVICE_TYPE_NONE) {
          avcodec_buffer_t derived_context;

          // Allow the hwdevice to prepare for this type of context to be derived
          if (encode_device->prepare_to_derive_context(platform_formats->avcodec_derived_dev_type)) {
            return nullptr;
          }

          auto err = av_hwdevice_ctx_create_derived(&derived_context, platform_formats->avcodec_derived_dev_type, encoding_stream_context.get(), 0);
          if (err) {
            std::array<char, AV_ERROR_MAX_STRING_SIZE> err_str {0};
            logging::error([&err_str, &err](std::ostream &out) {
              out << "Failed to derive device context: "sv << av_make_error_string(err_str.data(), AV_ERROR_MAX_STRING_SIZE, err);
            });

            return nullptr;
          }

          encoding_stream_context = std::move(derived_context);
        }

        // Initialize avcodec hardware frames
        {
          avcodec_buffer_t frame_ref {av_hwframe_ctx_alloc(encoding_stream_context.get())};

          auto frame_ctx = (AVHWFramesContext *) frame_ref->data;
          frame_ctx->format = ctx->pix_fmt;
          frame_ctx->sw_format = sw_fmt;
          frame_ctx->height = ctx->height;
          frame_ctx->width = ctx->width;
          frame_ctx->initial_pool_size = 0;

          // Allow the hwdevice to modify hwframe context parameters
          encode_device->init_hwframes(frame_ctx);

          if (auto err = av_hwframe_ctx_init(frame_ref.get()); err < 0) {
            return nullptr;
          }

          ctx->hw_frames_ctx = av_buffer_ref(frame_ref.get());
        }

        ctx->slices = config.slicesPerFrame;
      } else /* software */ {
        ctx->pix_fmt = sw_fmt;

        // Clients will request for the fewest slices per frame to get the
        // most efficient encode, but we may want to provide more slices than
        // requested to ensure we have enough parallelism for good performance.
        ctx->slices = std::max(config.slicesPerFrame, settings.video.min_threads);
      }

      if (encoder.flags & SINGLE_SLICE_ONLY) {
        ctx->slices = 1;
      }

      ctx->thread_type = FF_THREAD_SLICE;
      ctx->thread_count = ctx->slices;

      AVDictionary *options {nullptr};
      auto handle_option = [&options, &config](const encoder_t::option_t &option) {
        std::visit(
          util::overloaded {
            [&](int v) {
              av_dict_set_int(&options, option.name.c_str(), v, 0);
            },
            [&options, &option](const int *v) {
              av_dict_set_int(&options, option.name.c_str(), *v, 0);
            },
            [&options, &option](const std::optional<int> *v) {
              if (*v) {
                av_dict_set_int(&options, option.name.c_str(), **v, 0);
              }
            },
            [&](const std::function<int()> &v) {
              av_dict_set_int(&options, option.name.c_str(), v(), 0);
            },
            [&](const std::string &v) {
              av_dict_set(&options, option.name.c_str(), v.c_str(), 0);
            },
            [&options, &option](const std::string *v) {
              if (!v->empty()) {
                av_dict_set(&options, option.name.c_str(), v->c_str(), 0);
              }
            },
            [&](const std::function<const std::string(const config_t &cfg)> &v) {
              av_dict_set(&options, option.name.c_str(), v(config).c_str(), 0);
            }
          },
          option.value
        );
      };

      // Apply common options, then format-specific overrides
      for (auto &option : video_format.common_options) {
        handle_option(option);
      }
      for (auto &option : (config.dynamicRange ? video_format.hdr_options : video_format.sdr_options)) {
        handle_option(option);
      }
      if (config.chromaSamplingType == 1) {
        for (auto &option : (config.dynamicRange ? video_format.hdr444_options : video_format.sdr444_options)) {
          handle_option(option);
        }
      }
      if (retries > 0) {
        for (auto &option : video_format.fallback_options) {
          handle_option(option);
        }
      }

      auto bitrate = ((settings.video.max_bitrate > 0) ? std::min(config.bitrate, settings.video.max_bitrate) : config.bitrate) * 1000;
      logging::info("Streaming bitrate is {}", bitrate);
      ctx->rc_max_rate = bitrate;
      ctx->bit_rate = bitrate;

      if (encoder.flags & CBR_WITH_VBR) {
        // Ensure rc_max_bitrate != bit_rate to force VBR mode
        ctx->bit_rate--;
      } else {
        ctx->rc_min_rate = bitrate;
      }

      if (encoder.flags & RELAXED_COMPLIANCE) {
        ctx->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
      }

      if (!(encoder.flags & NO_RC_BUF_LIMIT)) {
        if (!hardware && (ctx->slices > 1 || config.videoFormat == 1)) {
          // Use a larger rc_buffer_size for software encoding when slices are enabled,
          // because libx264 can severely degrade quality if the buffer is too small.
          // libx265 encounters this issue more frequently, so always scale the
          // buffer by 1.5x for software HEVC encoding.
          ctx->rc_buffer_size = bitrate / ((config.framerate * 10) / 15);
        } else {
          ctx->rc_buffer_size = bitrate / config.framerate;

#ifndef __APPLE__
          if (encoder.name == "nvenc" && settings.video.nv_legacy.vbv_percentage_increase > 0) {
            ctx->rc_buffer_size += ctx->rc_buffer_size * settings.video.nv_legacy.vbv_percentage_increase / 100;
          }
#endif
        }
      }

      // Allow the encoding device a final opportunity to set/unset or override any options
      encode_device->init_codec_options(ctx.get(), &options);

      if (auto status = avcodec_open2(ctx.get(), codec, &options)) {
        std::array<char, AV_ERROR_MAX_STRING_SIZE> err_str {0};

        if (!video_format.fallback_options.empty() && retries == 0) {
          logging::info([&video_format, &err_str, &status](std::ostream &out) {
            out << "Retrying with fallback configuration options for ["sv << video_format.name << "] after error: "sv
                << av_make_error_string(err_str.data(), AV_ERROR_MAX_STRING_SIZE, status);
          });

          continue;
        } else {
          logging::error([&video_format, &err_str, &status](std::ostream &out) {
            out << "Could not open codec ["sv
                << video_format.name << "]: "sv
                << av_make_error_string(err_str.data(), AV_ERROR_MAX_STRING_SIZE, status);
          });

          return nullptr;
        }
      }

      // Successfully opened the codec
      break;
    }

    avcodec_frame_t frame {av_frame_alloc()};
    frame->format = ctx->pix_fmt;
    frame->width = ctx->width;
    frame->height = ctx->height;
    frame->color_range = ctx->color_range;
    frame->color_primaries = ctx->color_primaries;
    frame->color_trc = ctx->color_trc;
    frame->colorspace = ctx->colorspace;
    frame->chroma_location = ctx->chroma_sample_location;

    // Attach HDR metadata to the AVFrame
    if (colorspace_is_hdr(colorspace)) {
      SS_HDR_METADATA hdr_metadata;
      if (disp->get_hdr_metadata(hdr_metadata)) {
        auto mdm = av_mastering_display_metadata_create_side_data(frame.get());

        mdm->display_primaries[0][0] = av_make_q(hdr_metadata.displayPrimaries[0].x, 50000);
        mdm->display_primaries[0][1] = av_make_q(hdr_metadata.displayPrimaries[0].y, 50000);
        mdm->display_primaries[1][0] = av_make_q(hdr_metadata.displayPrimaries[1].x, 50000);
        mdm->display_primaries[1][1] = av_make_q(hdr_metadata.displayPrimaries[1].y, 50000);
        mdm->display_primaries[2][0] = av_make_q(hdr_metadata.displayPrimaries[2].x, 50000);
        mdm->display_primaries[2][1] = av_make_q(hdr_metadata.displayPrimaries[2].y, 50000);

        mdm->white_point[0] = av_make_q(hdr_metadata.whitePoint.x, 50000);
        mdm->white_point[1] = av_make_q(hdr_metadata.whitePoint.y, 50000);

        mdm->min_luminance = av_make_q(hdr_metadata.minDisplayLuminance, 10000);
        mdm->max_luminance = av_make_q(hdr_metadata.maxDisplayLuminance, 1);

        mdm->has_luminance = hdr_metadata.maxDisplayLuminance != 0 ? 1 : 0;
        mdm->has_primaries = hdr_metadata.displayPrimaries[0].x != 0 ? 1 : 0;

        if (hdr_metadata.maxContentLightLevel != 0 || hdr_metadata.maxFrameAverageLightLevel != 0) {
          auto clm = av_content_light_metadata_create_side_data(frame.get());

          clm->MaxCLL = hdr_metadata.maxContentLightLevel;
          clm->MaxFALL = hdr_metadata.maxFrameAverageLightLevel;
        }
      } else {
        logging::error("Couldn't get display hdr metadata when colorspace selection indicates it should have one");
      }
    }

    std::unique_ptr<platf::avcodec_encode_device_t> encode_device_final;

    if (!encode_device->data) {
      auto software_encode_device = std::make_unique<avcodec_software_encode_device_t>(settings.video.min_threads);

      if (software_encode_device->init(width, height, frame.get(), sw_fmt, hardware)) {
        return nullptr;
      }
      software_encode_device->colorspace = colorspace;

      encode_device_final = std::move(software_encode_device);
    } else {
      encode_device_final = std::move(encode_device);
    }

    if (encode_device_final->set_frame(frame.release(), ctx->hw_frames_ctx)) {
      return nullptr;
    }

    encode_device_final->apply_colorspace();

    auto session = std::make_unique<avcodec_encode_session_t>(
      std::move(ctx),
      std::move(encode_device_final),

      // 0 ==> don't inject, 1 ==> inject for h264, 2 ==> inject for hevc
      config.videoFormat <= 1 ? (1 - static_cast<int>(video_format[encoder_t::VUI_PARAMETERS])) * (1 + config.videoFormat) : 0
    );

    return session;
  }

  /**
   * @brief Create NVENC encode session.
   *
   * @param client_config Client stream configuration negotiated for this session.
   * @param encode_device Encode device.
   * @return Constructed NVENC encode session object.
   */
  std::unique_ptr<nvenc_encode_session_t> make_nvenc_encode_session(const config_t &client_config, std::unique_ptr<platf::nvenc_encode_device_t> encode_device) {
    if (!encode_device->init_encoder(client_config, encode_device->colorspace)) {
      return nullptr;
    }

    return std::make_unique<nvenc_encode_session_t>(std::move(encode_device));
  }

  /**
   * @brief Create encode session.
   *
   * @param settings Host encoder settings used by the session.
   * @param disp Display connection or display handle.
   * @param encoder Encoder configuration or encoder instance.
   * @param config Configuration values to apply.
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @param encode_device Encode device.
   * @return Constructed encode session object.
   */
  std::unique_ptr<encode_session_t> make_encode_session(const config::settings_t &settings, platf::display_t *disp, const encoder_t &encoder, const config_t &config, int width, int height, std::unique_ptr<platf::encode_device_t> encode_device) {
    if (dynamic_cast<platf::avcodec_encode_device_t *>(encode_device.get())) {
      auto avcodec_encode_device = boost::dynamic_pointer_cast<platf::avcodec_encode_device_t>(std::move(encode_device));
      return make_avcodec_encode_session(settings, disp, encoder, config, width, height, std::move(avcodec_encode_device));
    } else if (dynamic_cast<platf::nvenc_encode_device_t *>(encode_device.get())) {
      auto nvenc_encode_device = boost::dynamic_pointer_cast<platf::nvenc_encode_device_t>(std::move(encode_device));
      return make_nvenc_encode_session(config, std::move(nvenc_encode_device));
    }

    return nullptr;
  }

  /**
   * @brief Apply reference invalidations followed by a pending IDR request in mailbox order.
   * @param session Active encoder receiving reference and frame-type requests.
   * @param invalidations Retained invalidation event drained before IDR handling.
   * @param idr Retained IDR event consumed once per iteration.
   * @return Whether an IDR frame was requested for this iteration.
   */
  bool apply_encoder_requests(encode_session_t &session, const safe::mail_raw_t::event_t<std::pair<int64_t, int64_t>> &invalidations, const safe::mail_raw_t::event_t<bool> &idr) {
    while (invalidations->peek()) {
      if (auto frames = invalidations->pop(0ms)) {
        session.invalidate_ref_frames(frames->first, frames->second);
      }
    }
    const bool requested = idr->peek();
    if (requested) {
      idr->pop();
      session.request_idr_frame();
    }
    return requested;
  }

  /**
   * @brief Initialize an encoder frame from a short-lived dummy capture image.
   * @param display Display allocating and filling the temporary image.
   * @param session Encoder receiving the initialized image.
   * @return Whether allocation, filling and conversion succeeded.
   */
  bool initialize_dummy_frame(platf::display_t &display, encode_session_t &session) {
    auto image = display.alloc_img();
    return image && !display.dummy_img(image.get()) && !session.convert(*image);
  }

  /**
   * @brief Borrow one encode stream's frame progress, display lifecycle and transport binding.
   */
  struct encode_stream_binding_t {
    int &frame_nr;  ///< Frame progress retained by the caller across encoder reinitialization.
    safe::signal_t &reinit_event;  ///< Capture-side request to replace the active display.
    const encoder_t &encoder;  ///< Selected encoder retained by the host for this stream.
    stream::session_t *channel_data;  ///< Borrowed transport session that outlives this loop and its queued packets.
  };

  /**
   * @brief Run one encode loop for a display capture stream.
   *
   * @param binding Borrowed stream progress, display reinitialization signal, encoder and transport session.
   * @param mail Session mail bus.
   * @param images Captured image event source.
   * @param config Video configuration.
   * @param disp Display being encoded.
   * @param encode_device Platform encode device.
   * @param host Host input or video service and its capture dependencies.
   */
  void encode_run(service_t::impl_t &host, const encode_stream_binding_t &binding, safe::mail_t mail, img_event_t images, config_t config, std::shared_ptr<platf::display_t> disp, std::unique_ptr<platf::encode_device_t> encode_device) {
    auto &frame_nr = binding.frame_nr;
    auto &reinit_event = binding.reinit_event;
    const auto &encoder = binding.encoder;
    const auto channel_data = binding.channel_data;
    auto session = make_encode_session(host.settings, disp.get(), encoder, config, disp->width, disp->height, std::move(encode_device));
    if (!session) {
      return;
    }

    // As a workaround for NVENC hangs and to generally speed up encoder reinit,
    // we will complete the encoder teardown in a separate thread if supported.
    // This will move expensive processing off the encoder thread to allow us
    // to restart encoding as soon as possible. For cases where the NVENC driver
    // hang occurs, this thread may probably never exit, but it will allow
    // streaming to continue without requiring a full restart of Sunshine.
    auto fail_guard = util::fail_guard([&encoder, &session] {
      if (encoder.flags & ASYNC_TEARDOWN) {
        std::jthread encoder_teardown_thread {[session = std::move(session)]() mutable {
          logging::info("Starting async encoder teardown");
          session.reset();
          logging::info("Async encoder teardown complete");
        }};
        encoder_teardown_thread.detach();
      }
    });

    // set max frame time based on client-requested target framerate.
    double minimum_fps_target = (host.settings.video.minimum_fps_target > 0.0) ? host.settings.video.minimum_fps_target : (config.framerate / 2);
    std::chrono::duration<double, std::milli> max_frametime {1000.0 / minimum_fps_target};
    logging::info([&minimum_fps_target, &max_frametime](std::ostream &out) {
      out << "Minimum FPS target set to ~"sv << minimum_fps_target << "fps ("sv << max_frametime.count() << "ms)"sv;
    });

    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = host.mail->queue<packet_t>(mail::video_packets);
    auto idr_events = mail->event<bool>(mail::idr);
    auto invalidate_ref_frames_events = mail->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames);

    if (!initialize_dummy_frame(*disp, *session)) {
      return;
    }

    while (true) {
      const bool requested_idr_frame = apply_encoder_requests(*session, invalidate_ref_frames_events, idr_events);

      std::optional<std::chrono::steady_clock::time_point> frame_timestamp;

      // Encode at a minimum FPS to avoid image quality issues with static content
      if (!requested_idr_frame || images->peek()) {
        if (auto img = images->pop(max_frametime)) {
          frame_timestamp = img->frame_timestamp;
          if (session->convert(*img)) {
            logging::error("Could not convert image"sv);
            return;
          }
        } else if (!images->running()) {
          break;
        }
      }

      // Break out of the encoding loop if any of the following are true:
      // a) The stream is ending
      // b) Sunshine is quitting
      // c) The capture side is waiting to reinit and we've encoded at least one frame
      //
      // If we have to reinit before we have received any captured frames, we will encode
      // the blank dummy frame just to let Moonlight know that we're alive.
      //
      // Ensure that this check occurs as close as possible to the encode call to prevent packets
      // in flight after encoder teardown.
      if (shutdown_event->peek() || !images->running() || (reinit_event.peek() && frame_nr > 1)) {
        break;
      }

      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {
        logging::error("Could not encode video packet"sv);
        return;
      }

      session->request_normal_frame();

      // While streaming check to see if the mouse is present and enable Mouse Keys to force the cursor to appear
      // This is useful for KVM switch scenarios where mouse may disappear during streaming
      host.platform.enable_mouse_keys();
    }
  }

  /**
   * @brief Create a port object or message.
   *
   * @param display Display object or identifier associated with the operation.
   * @param config Configuration values to apply.
   * @return Constructed port object.
   */
  input::touch_port_t make_port(platf::display_t *display, const config_t &config) {
    float wd = display->width;
    float hd = display->height;

    float wt = config.width;
    float ht = config.height;

    auto scalar = std::fminf(wt / wd, ht / hd);

    // we initialize scalar_tpcoords and logical dimensions to default values in case they are not set (non-KMS)
    float scalar_tpcoords = 1.0f;
    int display_env_logical_width = 0;
    int display_env_logical_height = 0;
    if (display->logical_width > 0 && display->logical_height > 0 && display->env_logical_width > 0 && display->env_logical_height > 0) {
      float lwd = display->logical_width;
      float lhd = display->logical_height;
      scalar_tpcoords = std::fminf(wd / lwd, hd / lhd);
      display_env_logical_width = display->env_logical_width;
      display_env_logical_height = display->env_logical_height;
    }

    auto w2 = scalar * wd;
    auto h2 = scalar * hd;

    auto offsetX = (config.width - w2) * 0.5f;
    auto offsetY = (config.height - h2) * 0.5f;

    return input::touch_port_t {
      {
        display->offset_x,
        display->offset_y,
        config.width,
        config.height,
        display->logical_width,
        display->logical_height,
        display->env_offset_x,
        display->env_offset_y,
      },
      display->env_width,
      display->env_height,
      offsetX,
      offsetY,
      1.0f / scalar,
      scalar_tpcoords,
      display_env_logical_width,
      display_env_logical_height
    };
  }

  /**
   * @brief Create encode device.
   *
   * @param disp Display connection or display handle.
   * @param encoder Encoder configuration or encoder instance.
   * @param config Configuration values to apply.
   * @return Constructed encode device object.
   */
  std::unique_ptr<platf::encode_device_t> make_encode_device(platf::display_t &disp, const encoder_t &encoder, const config_t &config) {
    std::unique_ptr<platf::encode_device_t> result;

    auto colorspace = colorspace_from_client_config(config, disp.is_hdr());

    platf::pix_fmt_e pix_fmt;
    if (config.chromaSamplingType == 1) {
      // YUV 4:4:4
      if (!(encoder.flags & YUV444_SUPPORT)) {
        // Encoder can't support YUV 4:4:4 regardless of hardware capabilities
        return {};
      }
      pix_fmt = (colorspace.bit_depth == 10) ?
                  encoder.platform_formats->pix_fmt_yuv444_10bit :
                  encoder.platform_formats->pix_fmt_yuv444_8bit;
    } else {
      // YUV 4:2:0
      pix_fmt = (colorspace.bit_depth == 10) ?
                  encoder.platform_formats->pix_fmt_10bit :
                  encoder.platform_formats->pix_fmt_8bit;
    }

    {
      auto encoder_name = encoder.codec_from_config(config).name;

      logging::info("Creating encoder [{}]", encoder_name);

      logging::info("Color coding: {}", colorspace_to_string(colorspace.colorspace));
      logging::info("Color depth: {}-bit", colorspace.bit_depth);
      logging::info("Color range: {}", (colorspace.full_range ? "JPEG" : "MPEG"));
    }

    if (dynamic_cast<const encoder_platform_formats_avcodec *>(encoder.platform_formats.get())) {
      result = disp.make_avcodec_encode_device(pix_fmt);
    } else if (dynamic_cast<const encoder_platform_formats_nvenc *>(encoder.platform_formats.get())) {
      result = disp.make_nvenc_encode_device(pix_fmt);
    }

    if (result) {
      result->colorspace = colorspace;
    }

    return result;
  }

  /**
   * @brief Create synced session.
   *
   * @param settings Host startup encoder configuration.
   * @param disp Display connection or display handle.
   * @param encoder Encoder configuration or encoder instance.
   * @param img Image or frame object to read from or populate.
   * @param ctx Native context object used by the operation or callback.
   * @return Constructed synced session object.
   */
  std::optional<sync_session_t> make_synced_session(const config::settings_t &settings, platf::display_t *disp, const encoder_t &encoder, platf::img_t &img, sync_session_ctx_t &ctx) {
    sync_session_t encode_session;

    encode_session.ctx = &ctx;

    auto encode_device = make_encode_device(*disp, encoder, ctx.config);
    if (!encode_device) {
      return std::nullopt;
    }

    // absolute mouse coordinates require that the dimensions of the screen are known
    ctx.touch_port_events->raise(make_port(disp, ctx.config));

    // Update client with our current HDR display state
    hdr_info_t hdr_info = std::make_unique<hdr_info_raw_t>(false);
    if (colorspace_is_hdr(encode_device->colorspace)) {
      if (disp->get_hdr_metadata(hdr_info->metadata)) {
        hdr_info->enabled = true;
      } else {
        logging::error("Couldn't get display hdr metadata when colorspace selection indicates it should have one");
      }
    }
    ctx.hdr_events->raise(std::move(hdr_info));

    auto session = make_encode_session(settings, disp, encoder, ctx.config, img.width, img.height, std::move(encode_device));
    if (!session) {
      return std::nullopt;
    }

    // Load the initial image to prepare for encoding
    if (session->convert(img)) {
      logging::error("Could not convert initial image"sv);
      return std::nullopt;
    }

    encode_session.session = std::move(session);

    return encode_session;
  }

  /**
   * @brief Encode one synchronized session, stopping it on conversion or packetization failure.
   * @param active Encoder and its retained session context.
   * @param image Current captured image, possibly absent on a timeout.
   * @param captured Whether a new image needs conversion.
   * @return Whether the session encoded a frame and can advance its iterator.
   */
  bool encode_synchronized_frame(sync_session_t &active, const std::shared_ptr<platf::img_t> &image, bool captured) {
    auto &context = *active.ctx;
    if (context.idr_events->peek()) {
      active.session->request_idr_frame();
      context.idr_events->pop();
    }
    if (captured && active.session->convert(*image)) {
      logging::error("Could not convert image"sv);
      context.shutdown_event->raise(true);
      return false;
    }
    std::optional<std::chrono::steady_clock::time_point> timestamp;
    if (image) {
      timestamp = image->frame_timestamp;
    }
    if (encode(context.frame_nr++, *active.session, context.packets, context.channel_data, timestamp)) {
      logging::error("Could not encode video packet"sv);
      context.shutdown_event->raise(true);
      return false;
    }
    active.session->request_normal_frame();
    return true;
  }

  /**
   * @brief Remove stopping synchronized sessions and encode each remaining subscriber in order.
   * @param sessions Active encoder sessions.
   * @param contexts Stable owned contexts referenced by sessions.
   * @param image Current captured image.
   * @param captured Whether the image is newly captured.
   * @return False when removing a session leaves no active subscribers.
   */
  bool encode_synchronized_sessions(std::vector<sync_session_t> &sessions, std::vector<std::unique_ptr<sync_session_ctx_t>> &contexts, const std::shared_ptr<platf::img_t> &image, bool captured) {
    for (auto position = sessions.begin(); position != sessions.end();) {
      auto context = position->ctx;
      if (context->shutdown_event->peek()) {
        context->join_event->raise(true);
        position = sessions.erase(position);
        contexts.erase(std::find_if(contexts.begin(), contexts.end(), [context](const auto &candidate) {
          return candidate.get() == context;
        }));
        if (sessions.empty()) {
          return false;
        }
        continue;
      }
      if (!encode_synchronized_frame(*position, image, captured)) {
        continue;
      }
      ++position;
    }
    return true;
  }

  /**
   * @brief Borrow stable synchronized subscriber contexts and their active encoder sessions.
   */
  struct synchronized_subscribers_t {
    std::vector<std::unique_ptr<sync_session_ctx_t>> &contexts;  ///< Context owners retained until their encoder session is removed.
    std::vector<sync_session_t> &sessions;  ///< Active encoders referencing the stable owned contexts.
  };

  /**
   * @brief Add queued synchronized subscribers after retaining their stable context storage.
   * @param settings Host encoder configuration.
   * @param display Display shared by the synchronous capture run.
   * @param encoder Selected encoder.
   * @param image Current image used to initialize each new encoder.
   * @param queue Pending synchronized contexts.
   * @param subscribers Borrowed context owners and active encoders appended after initialization succeeds.
   * @param status Error status set only on encoder initialization failure.
   * @return Whether capture can continue after processing pending contexts.
   */
  bool add_synchronized_sessions(const config::settings_t &settings, platf::display_t &display, const encoder_t &encoder, const std::shared_ptr<platf::img_t> &image, encode_session_ctx_queue_t &queue, const synchronized_subscribers_t &subscribers, platf::capture_e &status) {
    auto &contexts = subscribers.contexts;
    auto &sessions = subscribers.sessions;
    while (queue.peek()) {
      auto context = queue.pop();
      if (!context) {
        return false;
      }
      contexts.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*context)));
      auto session = make_synced_session(settings, &display, encoder, *image, *contexts.back());
      if (!session) {
        status = platf::capture_e::error;
        return false;
      }
      sessions.emplace_back(std::move(*session));
    }
    return true;
  }

  /**
   * @brief Recreate retained synchronous encoder sessions from the new display's dummy image.
   * @param settings Host encoder configuration.
   * @param display Replacement display retained by this capture run.
   * @param encoder Selected encoder.
   * @param image Dummy image used to initialize every retained session.
   * @param contexts Stable context owners retained across display reinitialization.
   * @param sessions Successfully created encoders appended in subscriber order.
   * @return Whether every retained subscriber obtained an encoder session.
   */
  bool initialize_synchronized_sessions(const config::settings_t &settings, platf::display_t &display, const encoder_t &encoder, platf::img_t &image, const std::vector<std::unique_ptr<sync_session_ctx_t>> &contexts, std::vector<sync_session_t> &sessions) {
    for (const auto &context : contexts) {
      auto session = make_synced_session(settings, &display, encoder, image, *context);
      if (!session) {
        return false;
      }
      sessions.emplace_back(std::move(*session));
    }
    return true;
  }

  /**
   * @brief Run synchronized capture and encoding.
   *
   * @param synced_session_ctxs Active synchronized session contexts.
   * @param encode_session_ctx_queue Pending synchronized session context queue.
   * @param display_names Cached display names.
   * @param display_p Active display index.
   * @return Encoder loop result.
   * @param host Host input or video service and its capture dependencies.
   */
  encode_e encode_run_sync(service_t::impl_t &host, std::vector<std::unique_ptr<sync_session_ctx_t>> &synced_session_ctxs, encode_session_ctx_queue_t &encode_session_ctx_queue, std::vector<std::string> &display_names, int &display_p) {
    const auto &encoder = *host.chosen_encoder;

    std::shared_ptr<platf::display_t> disp;

    auto switch_display_event = host.mail->event<int>(mail::switch_display);

    if (synced_session_ctxs.empty()) {
      auto ctx = encode_session_ctx_queue.pop();
      if (!ctx) {
        return encode_e::ok;
      }

      synced_session_ctxs.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*ctx)));
    }

    if (!open_capture_display(host, disp, encoder.platform_formats->dev_type, {display_names, display_p, switch_display_event}, synced_session_ctxs.front()->config, encode_session_ctx_queue)) {
      return encode_e::error;
    }

    auto img = disp->alloc_img();
    if (!img || disp->dummy_img(img.get())) {
      return encode_e::error;
    }

    std::vector<sync_session_t> synced_sessions;
    if (!initialize_synchronized_sessions(host.settings, *disp, encoder, *img, synced_session_ctxs, synced_sessions)) {
      return encode_e::error;
    }

    auto ec = platf::capture_e::ok;
    while (encode_session_ctx_queue.running()) {
      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        if (!add_synchronized_sessions(host.settings, *disp, encoder, img, encode_session_ctx_queue, {synced_session_ctxs, synced_sessions}, ec)) {
          return false;
        }
        if (!encode_synchronized_sessions(synced_sessions, synced_session_ctxs, img, frame_captured)) {
          return false;
        }

        if (switch_display_event->peek()) {
          ec = platf::capture_e::reinit;
          return false;
        }

        return true;
      };

      auto pull_free_image_callback = [&img](std::shared_ptr<platf::img_t> &img_out) -> bool {
        img_out = img;
        img_out->frame_timestamp.reset();
        return true;
      };

      auto status = disp->capture(push_captured_image_callback, pull_free_image_callback, &host.cursor);
      switch (status) {
        case platf::capture_e::reinit:
        case platf::capture_e::error:
        case platf::capture_e::ok:
        case platf::capture_e::timeout:
        case platf::capture_e::interrupted:
          return ec != platf::capture_e::ok ? ec : status;
      }
    }

    return encode_e::ok;
  }

  /**
   * @brief Run synchronous capture and encode work on the capture thread.
   * @param host Host input or video service and its capture dependencies.
   */
  void captureThreadSync(service_t::impl_t &host) {
    auto ref = host.capture_thread_sync.ref();

    std::vector<std::unique_ptr<sync_session_ctx_t>> synced_session_ctxs;

    auto &ctx = ref->encode_session_ctx_queue;
    auto lg = util::fail_guard([&]() {
      ctx.stop();

      for (auto &ctx : synced_session_ctxs) {
        ctx->shutdown_event->raise(true);
        ctx->join_event->raise(true);
      }

      for (auto &ctx : ctx.unsafe()) {
        ctx.shutdown_event->raise(true);
        ctx.join_event->raise(true);
      }
    });

    // Encoding and capture takes place on this thread
    platf::set_thread_name("video::capture_sync");
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    std::vector<std::string> display_names;
    int display_p = -1;
    while (encode_run_sync(host, synced_session_ctxs, ctx, display_names, display_p) == encode_e::reinit) {}
  }

  /**
   * @brief Capture and encode video using the asynchronous capture thread.
   *
   * @param mail Session mail bus.
   * @param config Video configuration.
   * @param channel_data Borrowed transport session that must outlive capture and consumption of its queued packets.
   * @param host Host input or video service and its capture dependencies.
   */
  void capture_async(service_t::impl_t &host, safe::mail_t mail, config_t &config, stream::session_t *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);

    auto images = std::make_shared<img_event_t::element_type>();
    auto lg = util::fail_guard([&]() {
      images->stop();
      shutdown_event->raise(true);
    });

    auto ref = host.capture_thread_async.ref();
    if (!ref) {
      return;
    }

    ref->capture_ctx_queue->raise(capture_ctx_t {images, config});

    if (!ref->capture_ctx_queue->running()) {
      return;
    }

    int frame_nr = 1;

    auto touch_port_event = mail->event<input::touch_port_t>(mail::touch_port);
    auto hdr_event = mail->event<hdr_info_t>(mail::hdr);

    // Encoding takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    while (!shutdown_event->peek() && images->running()) {
      // Wait for the main capture event when the display is being reinitialized
      if (ref->reinit_event.peek()) {
        std::this_thread::sleep_for(20ms);
        continue;
      }
      // Wait for the display to be ready
      std::shared_ptr<platf::display_t> display;
      {
        auto lg = ref->display_wp.lock();
        if (ref->display_wp->expired()) {
          continue;
        }

        display = ref->display_wp->lock();
      }

      auto &encoder = *host.chosen_encoder;

      auto encode_device = make_encode_device(*display, encoder, config);
      if (!encode_device) {
        return;
      }

      // absolute mouse coordinates require that the dimensions of the screen are known
      touch_port_event->raise(make_port(display.get(), config));

      // Update client with our current HDR display state
      hdr_info_t hdr_info = std::make_unique<hdr_info_raw_t>(false);
      if (colorspace_is_hdr(encode_device->colorspace)) {
        if (display->get_hdr_metadata(hdr_info->metadata)) {
          hdr_info->enabled = true;
        } else {
          logging::error("Couldn't get display hdr metadata when colorspace selection indicates it should have one");
        }
      }
      hdr_event->raise(std::move(hdr_info));

      encode_run(host, {frame_nr, ref->reinit_event, *ref->encoder_p, channel_data}, mail, images, config, display, std::move(encode_device));
    }
  }

  /**
   * @brief Capture and encode video for a streaming session.
   *
   * @param mail Session mail bus.
   * @param config Client-requested video configuration, normalized before capture begins.
   * @param channel_data Borrowed transport session that must outlive capture and consumption of its queued packets.
   * @param host Host input or video service and its capture dependencies.
   */
  void capture(service_t::impl_t &host, safe::mail_t mail, config_t config, stream::session_t *channel_data) {
    config = resolve_dynamic_range(*host.chosen_encoder, config);

    auto idr_events = mail->event<bool>(mail::idr);

    idr_events->raise(true);
    if (host.chosen_encoder->flags & PARALLEL_ENCODING) {
      capture_async(host, std::move(mail), config, channel_data);
    } else {
      safe::signal_t join_event;
      auto ref = host.capture_thread_sync.ref();
      ref->encode_session_ctx_queue.raise(sync_session_ctx_t {
        &join_event,
        mail->event<bool>(mail::shutdown),
        host.mail->queue<packet_t>(mail::video_packets),
        std::move(idr_events),
        mail->event<hdr_info_t>(mail::hdr),
        mail->event<input::touch_port_t>(mail::touch_port),
        config,
        1,
        channel_data,
      });

      // Wait for join signal
      join_event.view();
    }
  }

  /**
   * @brief Enumerates supported validate flag options.
   */
  enum validate_flag_e {
    VUI_PARAMS = 0x01,  ///< VUI parameters
  };

  /**
   * @brief Validate config before it is used.
   *
   * @param disp Display connection or display handle.
   * @param encoder Encoder configuration or encoder instance.
   * @param config Configuration values to apply.
   * @return 0 when the selected encoder/device accepts the configuration; nonzero otherwise.
   * @param host Host input or video service and its capture dependencies.
   */
  int validate_config(service_t::impl_t &host, std::shared_ptr<platf::display_t> disp, const encoder_t &encoder, const config_t &config) {
    auto encode_device = make_encode_device(*disp, encoder, config);
    if (!encode_device) {
      return -1;
    }

    auto session = make_encode_session(host.settings, disp.get(), encoder, config, disp->width, disp->height, std::move(encode_device));
    if (!session) {
      return -1;
    }

    {
      // Image buffers are large, so we use a separate scope to free it immediately after convert()
      auto img = disp->alloc_img();
      if (!img || disp->dummy_img(img.get()) || session->convert(*img)) {
        return -1;
      }
    }

    session->request_idr_frame();

    auto packets = host.mail->queue<packet_t>(mail::video_packets);
    while (!packets->peek()) {
      if (encode(1, *session, packets, nullptr, {})) {
        return -1;
      }
    }

    auto packet = packets->pop();
    if (!packet->is_idr()) {
      logging::error("First packet type is not an IDR frame"sv);

      return -1;
    }

    int flag = 0;

    // This check only applies for H.264 and HEVC
    if (config.videoFormat <= 1) {
      if (auto packet_avcodec = dynamic_cast<packet_raw_avcodec *>(packet.get())) {
        if (cbs::validate_sps(packet_avcodec->av_packet, config.videoFormat ? AV_CODEC_ID_H265 : AV_CODEC_ID_H264)) {
          flag |= VUI_PARAMS;
        }
      } else {
        // Don't check it for non-avcodec encoders.
        flag |= VUI_PARAMS;
      }
    }

    return flag;
  }

  /**
   * @brief Publish reference-limit and packet-header results without changing validation-result bit semantics.
   * @param codec Codec capabilities being updated.
   * @param maximum Result of probing explicitly limited reference frames.
   * @param automatic Result of probing automatically selected reference frames.
   */
  void publish_reference_probe(encoder_t::codec_t &codec, int maximum, int automatic) {
    codec[encoder_t::VUI_PARAMETERS] = (maximum & VUI_PARAMS) && (automatic & VUI_PARAMS);
    codec[encoder_t::REF_FRAMES_RESTRICT] = maximum >= 0;
    codec[encoder_t::PASSED] = maximum >= 0 || automatic >= 0;
  }

  /**
   * @brief Probe H.264 reference configurations, prioritizing quick rejection of unexpected encoders.
   * @param host Host validation dependencies.
   * @param display Display retained for both probe configurations.
   * @param encoder Encoder whose H.264 capabilities are updated.
   * @param maximum Explicit reference-frame probe configuration.
   * @param automatic Automatic reference-frame probe configuration.
   * @param expect_failure Whether automatic references should be tried first.
   * @param maximum_result Explicit-reference result needed by later codec probes.
   * @return Whether either usable H.264 configuration was found.
   */
  bool probe_h264_references(service_t::impl_t &host, const std::shared_ptr<platf::display_t> &display, encoder_t &encoder, const config_t &maximum, const config_t &automatic, bool expect_failure, int &maximum_result) {
    maximum_result = expect_failure ? -1 : validate_config(host, display, encoder, maximum);
    const auto automatic_result = maximum_result >= 0 ? maximum_result : validate_config(host, display, encoder, automatic);
    if (automatic_result < 0) {
      return false;
    }
    if (expect_failure) {
      maximum_result = validate_config(host, display, encoder, maximum);
    }
    publish_reference_probe(encoder.h264, maximum_result, automatic_result);
    return true;
  }

  /**
   * @brief Borrow an optional codec's capabilities together with its format and probing policy.
   */
  struct optional_codec_probe_t {
    encoder_t::codec_t &codec;  ///< Codec capabilities updated or cleared by the probe.
    int format;  ///< Protocol codec number used by both reference configurations.
    bool enabled;  ///< Whether configuration requests probing this codec.
  };

  /**
   * @brief Probe HEVC or AV1 reference configurations with the existing H.264 success constraint.
   * @param host Host validation dependencies.
   * @param display Display retained for reference probes.
   * @param encoder Selected encoder descriptor.
   * @param probe Borrowed codec capabilities, protocol format and enabled policy.
   * @param h264_maximum H.264 explicit-reference result controlling fallback probing.
   * @param maximum Explicit-reference probe configuration copied for this codec.
   * @param automatic Automatic-reference probe configuration copied for this codec.
   */
  void probe_secondary_references(service_t::impl_t &host, const std::shared_ptr<platf::display_t> &display, encoder_t &encoder, const optional_codec_probe_t &probe, int h264_maximum, config_t maximum, config_t automatic) {
    auto &codec = probe.codec;
    const auto format = probe.format;
    const auto enabled = probe.enabled;
    if (!enabled) {
      codec.capabilities.reset();
      return;
    }
    maximum.videoFormat = format;
    automatic.videoFormat = format;
    if (!display->is_codec_supported(codec.name, automatic)) {
      logging::info("Encoder [{}] is not supported on this GPU", codec.name);
      codec.capabilities.reset();
      return;
    }
    const auto maximum_result = validate_config(host, display, encoder, maximum);
    const auto automatic_result = (maximum_result >= 0 || h264_maximum >= 0) ? maximum_result : validate_config(host, display, encoder, automatic);
    publish_reference_probe(codec, maximum_result, automatic_result);
  }

  /**
   * @brief Probe a codec pixel mode after reopening the display for that exact configuration.
   * @param host Host display and validation dependencies.
   * @param display Display reset before checking the codec's baseline viability.
   * @param encoder Encoder descriptor supplying chroma support and codec names.
   * @param output Selected output name.
   * @param codec Capabilities of the codec being tested.
   * @param config Exact SDR/HDR and chroma configuration to validate.
   * @param flag Capability set from this probe's result.
   */
  void probe_codec_pixel_mode(service_t::impl_t &host, std::shared_ptr<platf::display_t> &display, encoder_t &encoder, const std::string &output, encoder_t::codec_t &codec, const config_t &config, encoder_t::flag_e flag) {
    reset_display(host, display, encoder.platform_formats->dev_type, output, config);
    if (!display || !codec[encoder_t::PASSED]) {
      return;
    }
    const auto name = encoder.codec_from_config(config).name;
    const bool supports_chroma = config.chromaSamplingType != 1 || (encoder.flags & YUV444_SUPPORT);
    codec[flag] = supports_chroma && display->is_codec_supported(name, config) && validate_config(host, display, encoder, config) >= 0;
  }

  /**
   * @brief Probe SDR 4:4:4 and HDR formats in the established codec/display order.
   * @param host Host display and validation dependencies.
   * @param display Display replaced for every pixel-mode probe.
   * @param encoder Codec capabilities updated by each probe.
   * @param output Selected output name.
   */
  void probe_encoder_pixel_modes(service_t::impl_t &host, std::shared_ptr<platf::display_t> &display, encoder_t &encoder, const std::string &output) {
    probe_codec_pixel_mode(host, display, encoder, output, encoder.h264, detail::pixel_probe_config(0, false, true), encoder_t::YUV444);
    encoder.h264[encoder_t::DYNAMIC_RANGE] = false;
    encoder.h264[encoder_t::DYNAMIC_RANGE_YUV444] = false;
    probe_codec_pixel_mode(host, display, encoder, output, encoder.hevc, detail::pixel_probe_config(1, false, true), encoder_t::YUV444);
    probe_codec_pixel_mode(host, display, encoder, output, encoder.hevc, detail::pixel_probe_config(1, true, false), encoder_t::DYNAMIC_RANGE);
    probe_codec_pixel_mode(host, display, encoder, output, encoder.hevc, detail::pixel_probe_config(1, true, true), encoder_t::DYNAMIC_RANGE_YUV444);
    probe_codec_pixel_mode(host, display, encoder, output, encoder.av1, detail::pixel_probe_config(2, false, true), encoder_t::YUV444);
    probe_codec_pixel_mode(host, display, encoder, output, encoder.av1, detail::pixel_probe_config(2, true, false), encoder_t::DYNAMIC_RANGE);
    probe_codec_pixel_mode(host, display, encoder, output, encoder.av1, detail::pixel_probe_config(2, true, true), encoder_t::DYNAMIC_RANGE_YUV444);
  }

  /**
   * @brief Validate encoder before it is used.
   * @param host Host input or video service and its capture dependencies.
   * @param encoder Encoder descriptor whose supported formats are probed and updated.
   * @param expect_failure Whether an unavailable encoder is expected.
   * @return Whether the encoder can produce the requested formats.
   */
  bool validate_encoder(service_t::impl_t &host, encoder_t &encoder, bool expect_failure) {
    const auto output_name {host.displays.map_output_name(host.settings.video.output_name)};
    std::shared_ptr<platf::display_t> disp;

    logging::info("Trying encoder [{}]", encoder.name);
    auto fg = util::fail_guard([&]() {
      logging::info("Encoder [{}] failed", encoder.name);
    });

    const auto test_hevc = detail::should_probe_codec(host.probed.active_hevc_mode, encoder.flags & H264_ONLY);
    const auto test_av1 = detail::should_probe_codec(host.probed.active_av1_mode, encoder.flags & H264_ONLY);

    encoder.h264.capabilities.set();
    encoder.hevc.capabilities.set();
    encoder.av1.capabilities.set();

    // First, test encoder viability
    config_t config_max_ref_frames {1920, 1080, 60, 6000, 1000, 1, 1, 1, 0, 0, 0, 0};
    config_t config_autoselect {1920, 1080, 60, 6000, 1000, 1, 0, 1, 0, 0, 0, 0};

    // If the encoder isn't supported at all (not even H.264), bail early
    reset_display(host, disp, encoder.platform_formats->dev_type, output_name, config_autoselect);
    if (!disp) {
      return false;
    }
    if (!disp->is_codec_supported(encoder.h264.name, config_autoselect)) {
      fg.disable();
      logging::info("Encoder [{}] is not supported on this GPU", encoder.name);
      return false;
    }

    int max_ref_frames_h264;
    if (!probe_h264_references(host, disp, encoder, config_max_ref_frames, config_autoselect, expect_failure, max_ref_frames_h264)) {
      return false;
    }
    probe_secondary_references(host, disp, encoder, {encoder.hevc, 1, test_hevc}, max_ref_frames_h264, config_max_ref_frames, config_autoselect);
    probe_secondary_references(host, disp, encoder, {encoder.av1, 2, test_av1}, max_ref_frames_h264, config_max_ref_frames, config_autoselect);
    probe_encoder_pixel_modes(host, disp, encoder, output_name);

    encoder.h264[encoder_t::VUI_PARAMETERS] = encoder.h264[encoder_t::VUI_PARAMETERS] && !host.settings.sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];
    encoder.hevc[encoder_t::VUI_PARAMETERS] = encoder.hevc[encoder_t::VUI_PARAMETERS] && !host.settings.sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];

    if (!encoder.h264[encoder_t::VUI_PARAMETERS]) {
      logging::warning("{}: h264 missing sps->vui parameters", encoder.name);
    }
    if (encoder.hevc[encoder_t::PASSED] && !encoder.hevc[encoder_t::VUI_PARAMETERS]) {
      logging::warning("{}: hevc missing sps->vui parameters", encoder.name);
    }

    fg.disable();
    return true;
  }

  /**
   * @brief Relax an unsupported explicit codec constraint when an encoder is preferred.
   * @param encoder Selected encoder used in diagnostics.
   * @param codec Probed codec capabilities.
   * @param name Codec display name used in existing warning messages.
   * @param mode Active requirement reset to automatic when unsupported.
   */
  void relax_codec_requirement(const encoder_t &encoder, const encoder_t::codec_t &codec, std::string_view name, int &mode) {
    const auto missing = detail::unsupported_codec_requirement(codec, mode);
    if (!missing) {
      return;
    }
    if (missing->empty()) {
      logging::warning("Encoder [{}] does not support {} on this system", encoder.name, name);
    } else {
      logging::warning("Encoder [{}] does not support {} {} on this system", encoder.name, name, *missing);
    }
    mode = 0;
  }

  /**
   * @brief Prefer encoder availability over unsupported HEVC and AV1 constraints.
   * @param host Active codec requirements updated for the selected encoder.
   * @param encoder Selected encoder's probed capabilities.
   */
  void relax_encoder_requirements(service_t::impl_t &host, const encoder_t &encoder) {
    relax_codec_requirement(encoder, encoder.hevc, "HEVC", host.probed.active_hevc_mode);
    relax_codec_requirement(encoder, encoder.av1, "AV1", host.probed.active_av1_mode);
  }

  /**
   * @brief Validate a manually requested encoder and remove it from fallback candidates if unavailable.
   * @param host Requested encoder setting and selection result.
   * @param candidates Ordered encoder candidates, pruned after a failed requested probe.
   * @param previous Previously selected encoder controlling quick-rejection probing.
   */
  void select_requested_encoder(service_t::impl_t &host, std::vector<encoder_t *> &candidates, const encoder_t *previous) {
    if (host.settings.video.encoder.empty()) {
      return;
    }
    for (auto position = candidates.begin(); position != candidates.end(); ++position) {
      auto encoder = *position;
      if (encoder->name != host.settings.video.encoder) {
        continue;
      }
      if (!validate_encoder(host, *encoder, previous && previous != encoder)) {
        candidates.erase(position);
        break;
      }
      relax_encoder_requirements(host, *encoder);
      host.chosen_encoder = encoder;
      break;
    }
    if (!host.chosen_encoder) {
      logging::error("Couldn't find any working encoder matching [{}]", host.settings.video.encoder);
    }
  }

  /**
   * @brief Select the first viable encoder satisfying explicit codec requirements.
   * @param host Active codec requirements and selection result.
   * @param candidates Ordered candidates pruned only for failed viability checks.
   * @param previous Previously selected encoder controlling quick-rejection probing.
   */
  void select_codec_required_encoder(service_t::impl_t &host, std::vector<encoder_t *> &candidates, const encoder_t *previous) {
    if (host.chosen_encoder || (host.probed.active_hevc_mode < 2 && host.probed.active_av1_mode < 2)) {
      return;
    }
    for (auto position = candidates.begin(); position != candidates.end();) {
      auto encoder = *position;
      if (!validate_encoder(host, *encoder, previous && previous != encoder)) {
        position = candidates.erase(position);
        continue;
      }
      if (detail::codec_meets_requirements(encoder->hevc, host.probed.active_hevc_mode) && detail::codec_meets_requirements(encoder->av1, host.probed.active_av1_mode)) {
        host.chosen_encoder = encoder;
        break;
      }
      ++position;
    }
    if (!host.chosen_encoder) {
      logging::error("Couldn't find any working encoder that meets HEVC/AV1 requirements"sv);
    }
  }

  /**
   * @brief Select the first remaining viable encoder while relaxing unsupported codec constraints.
   * @param host Selection result and codec requirements.
   * @param candidates Ordered remaining candidates pruned on failed viability checks.
   * @param previous Previously selected encoder controlling quick-rejection probing.
   */
  void select_fallback_encoder(service_t::impl_t &host, std::vector<encoder_t *> &candidates, const encoder_t *previous) {
    if (host.chosen_encoder) {
      return;
    }
    for (auto position = candidates.begin(); position != candidates.end();) {
      auto encoder = *position;
      if (!validate_encoder(host, *encoder, previous && previous != encoder)) {
        position = candidates.erase(position);
        continue;
      }
      relax_encoder_requirements(host, *encoder);
      host.chosen_encoder = encoder;
      break;
    }
  }

  /**
   * @brief Log one available codec's capability flags and selected encoder name.
   * @param codec Codec capabilities being reported.
   * @param encoder Selected encoder name.
   * @param short_name Lowercase codec label used by debug output.
   * @param display_name Codec label used by discovery output.
   */
  void report_codec_capabilities(const encoder_t::codec_t &codec, std::string_view encoder, std::string_view short_name, std::string_view display_name) {
    logging::debug("------  {} ------", short_name);
    for (int index = 0; index < encoder_t::MAX_FLAGS; ++index) {
      const auto flag = static_cast<encoder_t::flag_e>(index);
      logging::debug([&codec, flag](std::ostream &out) {
        out << encoder_t::from_flag(flag) << (codec[flag] ? ": supported"sv : ": unsupported"sv);
      });
    }
    logging::debug("-------------------"sv);
    logging::info("Found {} encoder: {} [{}]", display_name, codec.name, encoder);
  }

  /**
   * @brief Publish selected encoder capabilities and infer automatic codec advertisement modes.
   * @param host Capability snapshot updated after successful encoder selection.
   */
  void publish_encoder_capabilities(service_t::impl_t &host) {
    const auto &encoder = *host.chosen_encoder;
    host.probed.ref_frames_invalidation = (encoder.flags & REF_FRAMES_INVALIDATION);
    host.probed.yuv444[0] = encoder.h264[encoder_t::PASSED] && encoder.h264[encoder_t::YUV444];
    host.probed.yuv444[1] = encoder.hevc[encoder_t::PASSED] && encoder.hevc[encoder_t::YUV444];
    host.probed.yuv444[2] = encoder.av1[encoder_t::PASSED] && encoder.av1[encoder_t::YUV444];
    report_codec_capabilities(encoder.h264, encoder.name, "h264", "H.264");
    if (encoder.hevc[encoder_t::PASSED]) {
      report_codec_capabilities(encoder.hevc, encoder.name, "hevc", "HEVC");
    }
    if (encoder.av1[encoder_t::PASSED]) {
      report_codec_capabilities(encoder.av1, encoder.name, "av1", "AV1");
    }
    if (host.probed.active_hevc_mode == 0) {
      host.probed.active_hevc_mode = detail::advertised_codec_mode(encoder.hevc, 0);
      logging::debug("ENCODER STATUS ACTIVE_HEVC_MODE: {}", host.probed.active_hevc_mode);
    }
    if (host.probed.active_av1_mode == 0) {
      host.probed.active_av1_mode = detail::advertised_codec_mode(encoder.av1, 0);
      logging::debug("ENCODER STATUS ACTIVE_AV1_MODE: {}", host.probed.active_av1_mode);
    }
  }

  /**
   * @brief Select a usable encoder and publish its supported formats for the host.
   * @param host Host video service and its capture dependencies.
   * @return Zero when a usable encoder is selected, nonzero otherwise.
   */
  int probe_encoders(service_t::impl_t &host) {
    if (!allow_encoder_probing(host.displays)) {
      // Error already logged
      return -1;
    }

    std::vector<encoder_t *> encoder_list;
    encoder_list.reserve(host.encoders.size());
    for (auto &encoder : host.encoders) {
      encoder_list.push_back(&encoder);
    }

    // If we already have a good encoder, check to see if another probe is required
    if (host.chosen_encoder && !(host.chosen_encoder->flags & ALWAYS_REPROBE) && !host.platform.needs_encoder_reenumeration()) {
      return 0;
    }

    // Restart encoder selection
    auto previous_encoder = host.chosen_encoder;
    host.chosen_encoder = nullptr;
    host.probed.active_hevc_mode = host.settings.video.hevc_mode;
    host.probed.active_av1_mode = host.settings.video.av1_mode;
    host.probed.ref_frames_invalidation = false;
    host.platform.set_capture_requirements(host.probed.active_hevc_mode);

    select_requested_encoder(host, encoder_list, previous_encoder);
    logging::info("// Testing for available encoders, this may generate errors. You can safely ignore those errors. //"sv);
    select_codec_required_encoder(host, encoder_list, previous_encoder);
    select_fallback_encoder(host, encoder_list, previous_encoder);

    if (host.chosen_encoder == nullptr) {
      const auto output_name {host.displays.map_output_name(host.settings.video.output_name)};
      logging::fatal("Unable to find display or encoder during startup."sv);
      if (!host.settings.video.adapter_name.empty() || !output_name.empty()) {
        logging::fatal("Please ensure your manually chosen GPU and monitor are connected and powered on."sv);
      } else {
        logging::fatal("Please check that a display is connected and powered on."sv);
      }
      return -1;
    }

    logging::info(""sv);
    logging::info("// Ignore any errors mentioned above, they are not relevant. //"sv);
    logging::info(""sv);

    publish_encoder_capabilities(host);

    return 0;
  }

  // Linux only declaration
  /**
   * @brief Callback signature for VA-API AVCodec hardware input initialization.
   */
  typedef int (*vaapi_init_avcodec_hardware_input_buffer_fn)(platf::avcodec_encode_device_t *encode_device, AVBufferRef **hw_device_buf);

  util::Either<avcodec_buffer_t, int> vaapi_init_avcodec_hardware_input_buffer(const config::settings_t &settings, platf::avcodec_encode_device_t *encode_device) {
    avcodec_buffer_t hw_device_buf;

    // If an egl hwdevice
    if (encode_device->data) {
      if (((vaapi_init_avcodec_hardware_input_buffer_fn) encode_device->data)(encode_device, &hw_device_buf)) {
        return -1;
      }

      return hw_device_buf;
    }

    auto render_device = platf::resolve_render_device(settings);

    if (auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VAAPI, render_device.empty() ? nullptr : render_device.c_str(), nullptr, 0); status < 0) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> string;
      logging::error([&string, &status](std::ostream &out) {
        out << "Failed to create a VAAPI device: "sv << av_make_error_string(string.data(), AV_ERROR_MAX_STRING_SIZE, status);
      });
      return -1;
    }

    return hw_device_buf;
  }

#ifdef SUNSHINE_BUILD_VULKAN
  /**
   * @brief Callback signature for Vulkan AVCodec hardware input initialization.
   */
  using vulkan_init_avcodec_hardware_input_buffer_fn = int (*)(platf::avcodec_encode_device_t *encode_device, AVBufferRef **hw_device_buf);

  util::Either<avcodec_buffer_t, int> vulkan_init_avcodec_hardware_input_buffer(const config::settings_t &settings, platf::avcodec_encode_device_t *encode_device) {
    avcodec_buffer_t hw_device_buf;

    if (encode_device && encode_device->data) {
      if (((vulkan_init_avcodec_hardware_input_buffer_fn) encode_device->data)(encode_device, &hw_device_buf)) {
        return -1;
      }
      return hw_device_buf;
    }

    // Try render device path first, auto-detecting the GPU with a connected display
    auto render_device = platf::resolve_render_device(settings);

    auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, render_device.c_str(), nullptr, 0);
    if (status >= 0) {
      logging::info("Using Vulkan device: {}", render_device);
      return hw_device_buf;
    }

    // Fallback: try device indices for multi-GPU systems
    const std::array<const char *, 4> devices = {"1", "0", "2", "3"};
    for (auto device : devices) {
      status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, device, nullptr, 0);
      if (status >= 0) {
        logging::info("Using Vulkan device index: {}", device);
        return hw_device_buf;
      }
    }

    logging::error("Failed to create a Vulkan device"sv);
    return -1;
  }
#endif

  /**
   * @brief Initialize AVCodec hardware input buffers for CUDA.
   * @note The uniform backend hook supplies an encode device, which this initializer does not need.
   * @return Hardware buffer on success, or an error code on failure.
   */
  util::Either<avcodec_buffer_t, int> cuda_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *) {
    avcodec_buffer_t hw_device_buf;

    if (auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 1 /* AV_CUDA_USE_PRIMARY_CONTEXT */); status < 0) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> string;
      logging::error([&string, &status](std::ostream &out) {
        out << "Failed to create a CUDA device: "sv << av_make_error_string(string.data(), AV_ERROR_MAX_STRING_SIZE, status);
      });
      return -1;
    }

    return hw_device_buf;
  }

  /**
   * @brief Initialize AVCodec hardware input buffers for VideoToolbox.
   * @note The uniform backend hook supplies an encode device, which this initializer does not need.
   * @return Hardware buffer on success, or an error code on failure.
   */
  util::Either<avcodec_buffer_t, int> vt_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *) {
    avcodec_buffer_t hw_device_buf;

    if (auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0); status < 0) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> string;
      logging::error([&string, &status](std::ostream &out) {
        out << "Failed to create a VideoToolbox device: "sv << av_make_error_string(string.data(), AV_ERROR_MAX_STRING_SIZE, status);
      });
      return -1;
    }

    return hw_device_buf;
  }

#ifdef _WIN32
}

/**
 * @brief No-op lock callback used when FFmpeg requires a D3D11VA lock function.
 */
void do_nothing(void *) {
}

namespace video {
  /**
   * @brief Create an FFmpeg D3D11VA hardware device from Sunshine's DXGI device.
   */
  util::Either<avcodec_buffer_t, int> dxgi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device) {
    avcodec_buffer_t ctx_buf {av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA)};
    auto ctx = (AVD3D11VADeviceContext *) ((AVHWDeviceContext *) ctx_buf->data)->hwctx;

    std::fill_n((std::uint8_t *) ctx, sizeof(AVD3D11VADeviceContext), 0);

    auto device = static_cast<ID3D11Device *>(encode_device->data);

    device->AddRef();
    ctx->device = device;

    ctx->lock_ctx = (void *) 1;
    ctx->lock = do_nothing;
    ctx->unlock = do_nothing;

    if (auto err = av_hwdevice_ctx_init(ctx_buf.get()); err) {
      std::array<char, AV_ERROR_MAX_STRING_SIZE> err_str {0};
      logging::error([&err_str, &err](std::ostream &out) {
        out << "Failed to create FFMpeg hardware device context: "sv << av_make_error_string(err_str.data(), AV_ERROR_MAX_STRING_SIZE, err);
      });

      return err;
    }

    return ctx_buf;
  }
#endif

  int start_capture_async(service_t::impl_t &host, capture_thread_async_ctx_t &capture_thread_ctx) {
    capture_thread_ctx.encoder_p = host.chosen_encoder;
    capture_thread_ctx.reinit_event.reset();

    capture_thread_ctx.capture_ctx_queue = std::make_shared<safe::queue_t<capture_ctx_t>>(30);

    capture_thread_ctx.capture_thread = std::jthread {
      captureThread,
      std::ref(host),
      capture_thread_ctx.capture_ctx_queue,
      std::ref(capture_thread_ctx.display_wp),
      std::ref(capture_thread_ctx.reinit_event),
      std::ref(*capture_thread_ctx.encoder_p)
    };

    return 0;
  }

  /**
   * @brief Stop capture async processing.
   */
  void end_capture_async(capture_thread_async_ctx_t &capture_thread_ctx) {
    capture_thread_ctx.capture_ctx_queue->stop();

    capture_thread_ctx.capture_thread.join();
  }

  /**
   * @brief Start capture sync.
   */
  int start_capture_sync(service_t::impl_t &host) {
    if (host.sync_thread.joinable()) {
      host.sync_thread.join();
    }
    host.sync_thread = std::jthread {&captureThreadSync, std::ref(host)};
    return 0;
  }

  /**
   * @brief Stop capture sync processing.
   */
  void end_capture_sync(capture_thread_sync_ctx_t &ctx) {
  }

  /**
   * @brief Map an FFmpeg hardware device type to Sunshine's capture memory type.
   */
  platf::mem_type_e map_base_dev_type(AVHWDeviceType type) {
    using enum platf::mem_type_e;
    switch (type) {
      case AV_HWDEVICE_TYPE_D3D11VA:
        return dxgi;
      case AV_HWDEVICE_TYPE_VAAPI:
        return vaapi;
#ifdef SUNSHINE_BUILD_VULKAN
      case AV_HWDEVICE_TYPE_VULKAN:
        return vulkan;
#endif
      case AV_HWDEVICE_TYPE_CUDA:
        return cuda;
      case AV_HWDEVICE_TYPE_NONE:
        return system;
      case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
        return videotoolbox;
      default:
        return unknown;
    }

    return unknown;
  }

  /**
   * @brief Map an FFmpeg pixel format to Sunshine's pixel format.
   */
  platf::pix_fmt_e map_pix_fmt(AVPixelFormat fmt) {
    using enum platf::pix_fmt_e;
    switch (fmt) {
      case AV_PIX_FMT_VUYX:
        return ayuv;
      case AV_PIX_FMT_XV30:
        return y410;
      case AV_PIX_FMT_YUV420P10:
        return yuv420p10;
      case AV_PIX_FMT_YUV420P:
        return yuv420p;
      case AV_PIX_FMT_NV12:
        return nv12;
      case AV_PIX_FMT_P010:
        return p010;
      case AV_PIX_FMT_YUV444P:
        return yuv444p;
      case AV_PIX_FMT_YUV444P16:
        return yuv444p16;
      default:
        return unknown;
    }

    return unknown;
  }

  service_t::service_t(const config::settings_t &settings, safe::mail_t mail, bool &cursor, display_device::manager_t &displays, platf::runtime_t &platform):
      impl_ {std::make_unique<impl_t>(settings, std::move(mail), cursor, displays, platform)} {
  }

  service_t::~service_t() = default;

  capabilities_t service_t::capabilities() const {
    std::lock_guard lock {impl_->probe_mutex};
    return impl_->probed;
  }

  int service_t::probe_encoders() {
    std::lock_guard lock {impl_->probe_mutex};
    impl_->platform.set_capture_requirements(impl_->settings.video.hevc_mode);
    const auto result = ::video::probe_encoders(*impl_);
    impl_->platform.set_capture_requirements(impl_->probed.active_hevc_mode);
    return result;
  }

  bool service_t::validate_encoder(encoder_t &encoder, bool expect_failure) {
    std::lock_guard lock {impl_->probe_mutex};
    return ::video::validate_encoder(*impl_, encoder, expect_failure);
  }

  void service_t::capture(safe::mail_t mail, const config_t &config, stream::session_t *channel_data) {
    if (!impl_->chosen_encoder) {
      mail->event<bool>(mail::shutdown)->raise(true);
      return;
    }
    ::video::capture(*impl_, std::move(mail), config, channel_data);
  }

}  // namespace video
