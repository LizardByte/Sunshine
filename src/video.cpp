/**
 * @file src/video.cpp
 * @brief todo
 */
#include <atomic>
#include <bitset>
#include <list>
#include <thread>

#include <boost/pointer_cast.hpp>

extern "C" {
#include <libavutil/mastering_display_metadata.h>
#include <libswscale/swscale.h>
}

#include "cbs.h"
#include "config.h"
#include "input.h"
#include "main.h"
#include "nvenc/nvenc_base.h"
#include "platform/common.h"
#include "sync.h"
#include "video.h"

#ifdef _WIN32
extern "C" {
  #include <libavutil/hwcontext_d3d11va.h>
}
#endif

using namespace std::literals;
namespace video {

  void
  free_ctx(AVCodecContext *ctx) {
    avcodec_free_context(&ctx);
  }

  void
  free_frame(AVFrame *frame) {
    av_frame_free(&frame);
  }

  void
  free_buffer(AVBufferRef *ref) {
    av_buffer_unref(&ref);
  }

  using avcodec_ctx_t = util::safe_ptr<AVCodecContext, free_ctx>;
  using avcodec_frame_t = util::safe_ptr<AVFrame, free_frame>;
  using avcodec_buffer_t = util::safe_ptr<AVBufferRef, free_buffer>;
  using sws_t = util::safe_ptr<SwsContext, sws_freeContext>;
  using img_event_t = std::shared_ptr<safe::event_t<std::shared_ptr<platf::img_t>>>;

  namespace nv {

    enum class profile_h264_e : int {
      baseline,
      main,
      high,
      high_444p,
    };

    enum class profile_hevc_e : int {
      main,
      main_10,
      rext,
    };
  }  // namespace nv

  namespace qsv {

    enum class profile_h264_e : int {
      baseline = 66,
      main = 77,
      high = 100,
    };

    enum class profile_hevc_e : int {
      main = 1,
      main_10 = 2,
    };
  }  // namespace qsv

  platf::mem_type_e
  map_base_dev_type(AVHWDeviceType type);
  platf::pix_fmt_e
  map_pix_fmt(AVPixelFormat fmt);

  util::Either<avcodec_buffer_t, int>
  dxgi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *);
  util::Either<avcodec_buffer_t, int>
  vaapi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *);
  util::Either<avcodec_buffer_t, int>
  cuda_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *);
  util::Either<avcodec_buffer_t, int>
  vt_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *);

  class avcodec_software_encode_device_t: public platf::avcodec_encode_device_t {
  public:
    int
    convert(platf::img_t &img) override {
      av_frame_make_writable(sw_frame.get());

      const int linesizes[2] {
        img.row_pitch, 0
      };

      std::uint8_t *data[4];

      data[0] = sw_frame->data[0] + offsetY;
      if (sw_frame->format == AV_PIX_FMT_NV12) {
        data[1] = sw_frame->data[1] + offsetUV * 2;
        data[2] = nullptr;
      }
      else {
        data[1] = sw_frame->data[1] + offsetUV;
        data[2] = sw_frame->data[2] + offsetUV;
        data[3] = nullptr;
      }

      int ret = sws_scale(sws.get(), (std::uint8_t *const *) &img.data, linesizes, 0, img.height, data, sw_frame->linesize);
      if (ret <= 0) {
        BOOST_LOG(error) << "Couldn't convert image to required format and/or size"sv;

        return -1;
      }

      // If frame is not a software frame, it means we still need to transfer from main memory
      // to vram memory
      if (frame->hw_frames_ctx) {
        auto status = av_hwframe_transfer_data(frame, sw_frame.get(), 0);
        if (status < 0) {
          char string[AV_ERROR_MAX_STRING_SIZE];
          BOOST_LOG(error) << "Failed to transfer image data to hardware frame: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
          return -1;
        }
      }

      return 0;
    }

    int
    set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override {
      this->frame = frame;

      // If it's a hwframe, allocate buffers for hardware
      if (hw_frames_ctx) {
        hw_frame.reset(frame);

        if (av_hwframe_get_buffer(hw_frames_ctx, frame, 0)) return -1;
      }
      else {
        sw_frame.reset(frame);
      }

      return 0;
    }

    void
    apply_colorspace() override {
      auto avcodec_colorspace = avcodec_colorspace_from_sunshine_colorspace(colorspace);
      sws_setColorspaceDetails(sws.get(),
        sws_getCoefficients(SWS_CS_DEFAULT), 0,
        sws_getCoefficients(avcodec_colorspace.software_format), avcodec_colorspace.range - 1,
        0, 1 << 16, 1 << 16);
    }

    /**
     * When preserving aspect ratio, ensure that padding is black
     */
    int
    prefill() {
      auto frame = sw_frame ? sw_frame.get() : this->frame;
      auto width = frame->width;
      auto height = frame->height;

      av_frame_get_buffer(frame, 0);
      sws_t sws {
        sws_getContext(
          width, height, AV_PIX_FMT_BGR0,
          width, height, (AVPixelFormat) frame->format,
          SWS_LANCZOS | SWS_ACCURATE_RND,
          nullptr, nullptr, nullptr)
      };

      if (!sws) {
        return -1;
      }

      util::buffer_t<std::uint32_t> img { (std::size_t)(width * height) };
      std::fill(std::begin(img), std::end(img), 0);

      const int linesizes[2] {
        width, 0
      };

      av_frame_make_writable(frame);

      auto data = img.begin();
      int ret = sws_scale(sws.get(), (std::uint8_t *const *) &data, linesizes, 0, height, frame->data, frame->linesize);
      if (ret <= 0) {
        BOOST_LOG(error) << "Couldn't convert image to required format and/or size"sv;

        return -1;
      }

      return 0;
    }

    int
    init(int in_width, int in_height, AVFrame *frame, AVPixelFormat format, bool hardware) {
      // If the device used is hardware, yet the image resides on main memory
      if (hardware) {
        sw_frame.reset(av_frame_alloc());

        sw_frame->width = frame->width;
        sw_frame->height = frame->height;
        sw_frame->format = format;
      }
      else {
        this->frame = frame;
      }

      if (prefill()) {
        return -1;
      }

      auto out_width = frame->width;
      auto out_height = frame->height;

      // Ensure aspect ratio is maintained
      auto scalar = std::fminf((float) out_width / in_width, (float) out_height / in_height);
      out_width = in_width * scalar;
      out_height = in_height * scalar;

      // result is always positive
      auto offsetW = (frame->width - out_width) / 2;
      auto offsetH = (frame->height - out_height) / 2;
      offsetUV = (offsetW + offsetH * frame->width / 2) / 2;
      offsetY = offsetW + offsetH * frame->width;

      sws.reset(sws_getContext(
        in_width, in_height, AV_PIX_FMT_BGR0,
        out_width, out_height, format,
        SWS_LANCZOS | SWS_ACCURATE_RND,
        nullptr, nullptr, nullptr));

      return sws ? 0 : -1;
    }

    // Store ownership when frame is hw_frame
    avcodec_frame_t hw_frame;

    avcodec_frame_t sw_frame;
    sws_t sws;

    // offset of input image to output frame in pixels
    int offsetUV;
    int offsetY;
  };

  enum flag_e : uint32_t {
    DEFAULT = 0,
    PARALLEL_ENCODING = 1 << 1,
    H264_ONLY = 1 << 2,  // When HEVC is too heavy
    LIMITED_GOP_SIZE = 1 << 3,  // Some encoders don't like it when you have an infinite GOP_SIZE. *cough* VAAPI *cough*
    SINGLE_SLICE_ONLY = 1 << 4,  // Never use multiple slices <-- Older intel iGPU's ruin it for everyone else :P
    CBR_WITH_VBR = 1 << 5,  // Use a VBR rate control mode to simulate CBR
    RELAXED_COMPLIANCE = 1 << 6,  // Use FF_COMPLIANCE_UNOFFICIAL compliance mode
    NO_RC_BUF_LIMIT = 1 << 7,  // Don't set rc_buffer_size
    REF_FRAMES_INVALIDATION = 1 << 8,  // Support reference frames invalidation
  };

  struct encoder_platform_formats_t {
    virtual ~encoder_platform_formats_t() = default;
    platf::mem_type_e dev_type;
    platf::pix_fmt_e pix_fmt_8bit, pix_fmt_10bit;
  };

  struct encoder_platform_formats_avcodec: encoder_platform_formats_t {
    using init_buffer_function_t = std::function<util::Either<avcodec_buffer_t, int>(platf::avcodec_encode_device_t *)>;

    encoder_platform_formats_avcodec(
      const AVHWDeviceType &avcodec_base_dev_type,
      const AVHWDeviceType &avcodec_derived_dev_type,
      const AVPixelFormat &avcodec_dev_pix_fmt,
      const AVPixelFormat &avcodec_pix_fmt_8bit,
      const AVPixelFormat &avcodec_pix_fmt_10bit,
      const init_buffer_function_t &init_avcodec_hardware_input_buffer_function):
        avcodec_base_dev_type { avcodec_base_dev_type },
        avcodec_derived_dev_type { avcodec_derived_dev_type },
        avcodec_dev_pix_fmt { avcodec_dev_pix_fmt },
        avcodec_pix_fmt_8bit { avcodec_pix_fmt_8bit },
        avcodec_pix_fmt_10bit { avcodec_pix_fmt_10bit },
        init_avcodec_hardware_input_buffer { init_avcodec_hardware_input_buffer_function } {
      dev_type = map_base_dev_type(avcodec_base_dev_type);
      pix_fmt_8bit = map_pix_fmt(avcodec_pix_fmt_8bit);
      pix_fmt_10bit = map_pix_fmt(avcodec_pix_fmt_10bit);
    }

    AVHWDeviceType avcodec_base_dev_type, avcodec_derived_dev_type;
    AVPixelFormat avcodec_dev_pix_fmt;
    AVPixelFormat avcodec_pix_fmt_8bit, avcodec_pix_fmt_10bit;

    init_buffer_function_t init_avcodec_hardware_input_buffer;
  };

  struct encoder_platform_formats_nvenc: encoder_platform_formats_t {
    encoder_platform_formats_nvenc(
      const platf::mem_type_e &dev_type,
      const platf::pix_fmt_e &pix_fmt_8bit,
      const platf::pix_fmt_e &pix_fmt_10bit) {
      encoder_platform_formats_t::dev_type = dev_type;
      encoder_platform_formats_t::pix_fmt_8bit = pix_fmt_8bit;
      encoder_platform_formats_t::pix_fmt_10bit = pix_fmt_10bit;
    }
  };

  struct encoder_t {
    std::string_view name;
    enum flag_e {
      PASSED,  // Is supported
      REF_FRAMES_RESTRICT,  // Set maximum reference frames
      CBR,  // Some encoders don't support CBR, if not supported --> attempt constant quantatication parameter instead
      DYNAMIC_RANGE,  // hdr
      VUI_PARAMETERS,  // AMD encoder with VAAPI doesn't add VUI parameters to SPS
      MAX_FLAGS
    };

    static std::string_view
    from_flag(flag_e flag) {
#define _CONVERT(x) \
  case flag_e::x:   \
    return #x##sv
      switch (flag) {
        _CONVERT(PASSED);
        _CONVERT(REF_FRAMES_RESTRICT);
        _CONVERT(CBR);
        _CONVERT(DYNAMIC_RANGE);
        _CONVERT(VUI_PARAMETERS);
        _CONVERT(MAX_FLAGS);
      }
#undef _CONVERT

      return "unknown"sv;
    }

    struct option_t {
      KITTY_DEFAULT_CONSTR_MOVE(option_t)
      option_t(const option_t &) = default;

      std::string name;
      std::variant<int, int *, std::optional<int> *, std::function<int()>, std::string, std::string *> value;

      option_t(std::string &&name, decltype(value) &&value):
          name { std::move(name) }, value { std::move(value) } {}
    };

    const std::unique_ptr<const encoder_platform_formats_t> platform_formats;

    struct {
      std::vector<option_t> common_options;
      std::vector<option_t> sdr_options;
      std::vector<option_t> hdr_options;
      std::optional<option_t> qp;

      std::string name;
      std::bitset<MAX_FLAGS> capabilities;

      bool
      operator[](flag_e flag) const {
        return capabilities[(std::size_t) flag];
      }

      std::bitset<MAX_FLAGS>::reference
      operator[](flag_e flag) {
        return capabilities[(std::size_t) flag];
      }
    } av1, hevc, h264;

    uint32_t flags;
  };

  struct encode_session_t {
    virtual ~encode_session_t() = default;

    virtual int
    convert(platf::img_t &img) = 0;

    virtual void
    request_idr_frame() = 0;

    virtual void
    request_normal_frame() = 0;

    virtual void
    invalidate_ref_frames(int64_t first_frame, int64_t last_frame) = 0;
  };

  class avcodec_encode_session_t: public encode_session_t {
  public:
    avcodec_encode_session_t() = default;
    avcodec_encode_session_t(avcodec_ctx_t &&avcodec_ctx, std::unique_ptr<platf::avcodec_encode_device_t> encode_device, int inject):
        avcodec_ctx { std::move(avcodec_ctx) }, device { std::move(encode_device) }, inject { inject } {}

    avcodec_encode_session_t(avcodec_encode_session_t &&other) noexcept = default;
    ~avcodec_encode_session_t() {
      // Order matters here because the context relies on the hwdevice still being valid
      avcodec_ctx.reset();
      device.reset();
    }

    // Ensure objects are destroyed in the correct order
    avcodec_encode_session_t &
    operator=(avcodec_encode_session_t &&other) {
      device = std::move(other.device);
      avcodec_ctx = std::move(other.avcodec_ctx);
      replacements = std::move(other.replacements);
      sps = std::move(other.sps);
      vps = std::move(other.vps);

      inject = other.inject;

      return *this;
    }

    int
    convert(platf::img_t &img) override {
      if (!device) return -1;
      return device->convert(img);
    }

    void
    request_idr_frame() override {
      if (device && device->frame) {
        auto &frame = device->frame;
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->flags |= AV_FRAME_FLAG_KEY;
      }
    }

    void
    request_normal_frame() override {
      if (device && device->frame) {
        auto &frame = device->frame;
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        frame->flags &= ~AV_FRAME_FLAG_KEY;
      }
    }

    void
    invalidate_ref_frames(int64_t first_frame, int64_t last_frame) override {
      BOOST_LOG(error) << "Encoder doesn't support reference frame invalidation";
      request_idr_frame();
    }

    avcodec_ctx_t avcodec_ctx;
    std::unique_ptr<platf::avcodec_encode_device_t> device;

    std::vector<packet_raw_t::replace_t> replacements;

    cbs::nal_t sps;
    cbs::nal_t vps;

    // inject sps/vps data into idr pictures
    int inject;
  };

  class nvenc_encode_session_t: public encode_session_t {
  public:
    nvenc_encode_session_t(std::unique_ptr<platf::nvenc_encode_device_t> encode_device):
        device(std::move(encode_device)) {
    }

    int
    convert(platf::img_t &img) override {
      if (!device) return -1;
      return device->convert(img);
    }

    void
    request_idr_frame() override {
      force_idr = true;
    }

    void
    request_normal_frame() override {
      force_idr = false;
    }

    void
    invalidate_ref_frames(int64_t first_frame, int64_t last_frame) override {
      if (!device || !device->nvenc) return;

      if (!device->nvenc->invalidate_ref_frames(first_frame, last_frame)) {
        force_idr = true;
      }
    }

    nvenc::nvenc_encoded_frame
    encode_frame(uint64_t frame_index) {
      if (!device || !device->nvenc) return {};

      auto result = device->nvenc->encode_frame(frame_index, force_idr);
      force_idr = false;
      return result;
    }

  private:
    std::unique_ptr<platf::nvenc_encode_device_t> device;
    bool force_idr = false;
  };

  struct sync_session_ctx_t {
    safe::signal_t *join_event;
    safe::mail_raw_t::event_t<bool> shutdown_event;
    safe::mail_raw_t::queue_t<packet_t> packets;
    safe::mail_raw_t::event_t<bool> idr_events;
    safe::mail_raw_t::event_t<hdr_info_t> hdr_events;
    safe::mail_raw_t::event_t<input::touch_port_t> touch_port_events;

    config_t config;
    int frame_nr;
    void *channel_data;
  };

  struct sync_session_t {
    sync_session_ctx_t *ctx;
    std::unique_ptr<encode_session_t> session;
  };

  using encode_session_ctx_queue_t = safe::queue_t<sync_session_ctx_t>;
  using encode_e = platf::capture_e;

  struct capture_ctx_t {
    img_event_t images;
    config_t config;
  };

  struct capture_thread_async_ctx_t {
    std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue;
    std::thread capture_thread;

    safe::signal_t reinit_event;
    const encoder_t *encoder_p;
    sync_util::sync_t<std::weak_ptr<platf::display_t>> display_wp;
  };

  struct capture_thread_sync_ctx_t {
    encode_session_ctx_queue_t encode_session_ctx_queue { 30 };
  };

  int
  start_capture_sync(capture_thread_sync_ctx_t &ctx);
  void
  end_capture_sync(capture_thread_sync_ctx_t &ctx);
  int
  start_capture_async(capture_thread_async_ctx_t &ctx);
  void
  end_capture_async(capture_thread_async_ctx_t &ctx);

  // Keep a reference counter to ensure the capture thread only runs when other threads have a reference to the capture thread
  auto capture_thread_async = safe::make_shared<capture_thread_async_ctx_t>(start_capture_async, end_capture_async);
  auto capture_thread_sync = safe::make_shared<capture_thread_sync_ctx_t>(start_capture_sync, end_capture_sync);

#ifdef _WIN32
  static encoder_t nvenc {
    "nvenc"sv,
    std::make_unique<encoder_platform_formats_nvenc>(
      platf::mem_type_e::dxgi,
      platf::pix_fmt_e::nv12, platf::pix_fmt_e::p010),
    {
      // Common options
      {},
      // SDR-specific options
      {},
      // HDR-specific options
      {},
      std::nullopt,  // QP
      "av1_nvenc"s,
    },
    {
      // Common options
      {},
      // SDR-specific options
      {},
      // HDR-specific options
      {},
      std::nullopt,  // QP
      "hevc_nvenc"s,
    },
    {
      // Common options
      {},
      // SDR-specific options
      {},
      // HDR-specific options
      {},
      std::nullopt,  // QP
      "h264_nvenc"s,
    },
    PARALLEL_ENCODING | REF_FRAMES_INVALIDATION  // flags
  };
#elif !defined(__APPLE__)
  static encoder_t nvenc {
    "nvenc"sv,
    std::make_unique<encoder_platform_formats_avcodec>(
  #ifdef _WIN32
      AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_NONE,
      AV_PIX_FMT_D3D11,
  #else
      AV_HWDEVICE_TYPE_CUDA, AV_HWDEVICE_TYPE_NONE,
      AV_PIX_FMT_CUDA,
  #endif
      AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
  #ifdef _WIN32
      dxgi_init_avcodec_hardware_input_buffer
  #else
      cuda_init_avcodec_hardware_input_buffer
  #endif
      ),
    {
      // Common options
      {
        { "delay"s, 0 },
        { "forced-idr"s, 1 },
        { "zerolatency"s, 1 },
        { "preset"s, &config::video.nv_legacy.preset },
        { "tune"s, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY },
        { "rc"s, NV_ENC_PARAMS_RC_CBR },
        { "multipass"s, &config::video.nv_legacy.multipass },
      },
      // SDR-specific options
      {},
      // HDR-specific options
      {},
      std::nullopt,
      "av1_nvenc"s,
    },
    {
      // Common options
      {
        { "delay"s, 0 },
        { "forced-idr"s, 1 },
        { "zerolatency"s, 1 },
        { "preset"s, &config::video.nv_legacy.preset },
        { "tune"s, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY },
        { "rc"s, NV_ENC_PARAMS_RC_CBR },
        { "multipass"s, &config::video.nv_legacy.multipass },
      },
      // SDR-specific options
      {
        { "profile"s, (int) nv::profile_hevc_e::main },
      },
      // HDR-specific options
      {
        { "profile"s, (int) nv::profile_hevc_e::main_10 },
      },
      std::nullopt,
      "hevc_nvenc"s,
    },
    {
      {
        { "delay"s, 0 },
        { "forced-idr"s, 1 },
        { "zerolatency"s, 1 },
        { "preset"s, &config::video.nv_legacy.preset },
        { "tune"s, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY },
        { "rc"s, NV_ENC_PARAMS_RC_CBR },
        { "coder"s, &config::video.nv_legacy.h264_coder },
        { "multipass"s, &config::video.nv_legacy.multipass },
      },
      // SDR-specific options
      {
        { "profile"s, (int) nv::profile_h264_e::high },
      },
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
      "h264_nvenc"s,
    },
    PARALLEL_ENCODING
  };
#endif

#ifdef _WIN32
  static encoder_t quicksync {
    "quicksync"sv,
    std::make_unique<encoder_platform_formats_avcodec>(
      AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_QSV,
      AV_PIX_FMT_QSV,
      AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
      dxgi_init_avcodec_hardware_input_buffer),
    {
      // Common options
      {
        { "preset"s, &config::video.qsv.qsv_preset },
        { "forced_idr"s, 1 },
        { "async_depth"s, 1 },
        { "low_delay_brc"s, 1 },
        { "low_power"s, 1 },
      },
      // SDR-specific options
      {},
      // HDR-specific options
      {},
      std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
      "av1_qsv"s,
    },
    {
      // Common options
      {
        { "preset"s, &config::video.qsv.qsv_preset },
        { "forced_idr"s, 1 },
        { "async_depth"s, 1 },
        { "low_delay_brc"s, 1 },
        { "low_power"s, 1 },
        { "recovery_point_sei"s, 0 },
        { "pic_timing_sei"s, 0 },
      },
      // SDR-specific options
      {
        { "profile"s, (int) qsv::profile_hevc_e::main },
      },
      // HDR-specific options
      {
        { "profile"s, (int) qsv::profile_hevc_e::main_10 },
      },
      std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
      "hevc_qsv"s,
    },
    {
      // Common options
      {
        { "preset"s, &config::video.qsv.qsv_preset },
        { "cavlc"s, &config::video.qsv.qsv_cavlc },
        { "forced_idr"s, 1 },
        { "async_depth"s, 1 },
        { "low_delay_brc"s, 1 },
        { "low_power"s, 1 },
        { "recovery_point_sei"s, 0 },
        { "vcm"s, 1 },
        { "pic_timing_sei"s, 0 },
        { "max_dec_frame_buffering"s, 1 },
      },
      // SDR-specific options
      {
        { "profile"s, (int) qsv::profile_h264_e::high },
      },
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
      "h264_qsv"s,
    },
    PARALLEL_ENCODING | CBR_WITH_VBR | RELAXED_COMPLIANCE | NO_RC_BUF_LIMIT
  };

  static encoder_t amdvce {
    "amdvce"sv,
    std::make_unique<encoder_platform_formats_avcodec>(
      AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_NONE,
      AV_PIX_FMT_D3D11,
      AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
      dxgi_init_avcodec_hardware_input_buffer),
    {
      // Common options
      {
        { "filler_data"s, false },
        { "log_to_dbg"s, []() { return config::sunshine.min_log_level < 2 ? 1 : 0; } },
        { "preencode"s, &config::video.amd.amd_preanalysis },
        { "quality"s, &config::video.amd.amd_quality_av1 },
        { "rc"s, &config::video.amd.amd_rc_av1 },
        { "usage"s, &config::video.amd.amd_usage_av1 },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp_p"s, &config::video.qp }),
      "av1_amf"s,
    },
    {
      // Common options
      {
        { "filler_data"s, false },
        { "log_to_dbg"s, []() { return config::sunshine.min_log_level < 2 ? 1 : 0; } },
        { "gops_per_idr"s, 1 },
        { "header_insertion_mode"s, "idr"s },
        { "preencode"s, &config::video.amd.amd_preanalysis },
        { "qmax"s, 51 },
        { "qmin"s, 0 },
        { "quality"s, &config::video.amd.amd_quality_hevc },
        { "rc"s, &config::video.amd.amd_rc_hevc },
        { "usage"s, &config::video.amd.amd_usage_hevc },
        { "vbaq"s, &config::video.amd.amd_vbaq },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp_p"s, &config::video.qp }),
      "hevc_amf"s,
    },
    {
      // Common options
      {
        { "filler_data"s, false },
        { "log_to_dbg"s, []() { return config::sunshine.min_log_level < 2 ? 1 : 0; } },
        { "preencode"s, &config::video.amd.amd_preanalysis },
        { "qmax"s, 51 },
        { "qmin"s, 0 },
        { "quality"s, &config::video.amd.amd_quality_h264 },
        { "rc"s, &config::video.amd.amd_rc_h264 },
        { "usage"s, &config::video.amd.amd_usage_h264 },
        { "vbaq"s, &config::video.amd.amd_vbaq },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp_p"s, &config::video.qp }),
      "h264_amf"s,
    },
    PARALLEL_ENCODING
  };
#endif

  static encoder_t software {
    "software"sv,
    std::make_unique<encoder_platform_formats_avcodec>(
      AV_HWDEVICE_TYPE_NONE, AV_HWDEVICE_TYPE_NONE,
      AV_PIX_FMT_NONE,
      AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10,
      nullptr),
    {
      // libsvtav1 takes different presets than libx264/libx265.
      // We set an infinite GOP length, use a low delay prediction structure,
      // force I frames to be key frames, and set max bitrate to default to work
      // around a FFmpeg bug with CBR mode.
      {
        { "svtav1-params"s, "keyint=-1:pred-struct=1:force-key-frames=1:mbr=0"s },
        { "preset"s, &config::video.sw.svtav1_preset },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),

#ifdef ENABLE_BROKEN_AV1_ENCODER
      // Due to bugs preventing on-demand IDR frames from working and very poor
      // real-time encoding performance, we do not enable libsvtav1 by default.
      // It is only suitable for testing AV1 until the IDR frame issue is fixed.
      "libsvtav1"s,
#else
      {},
#endif
    },
    {
      // x265's Info SEI is so long that it causes the IDR picture data to be
      // kicked to the 2nd packet in the frame, breaking Moonlight's parsing logic.
      // It also looks like gop_size isn't passed on to x265, so we have to set
      // 'keyint=-1' in the parameters ourselves.
      {
        { "forced-idr"s, 1 },
        { "x265-params"s, "info=0:keyint=-1"s },
        { "preset"s, &config::video.sw.sw_preset },
        { "tune"s, &config::video.sw.sw_tune },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "libx265"s,
    },
    {
      // Common options
      {
        { "preset"s, &config::video.sw.sw_preset },
        { "tune"s, &config::video.sw.sw_tune },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "libx264"s,
    },
    H264_ONLY | PARALLEL_ENCODING
  };

#ifdef __linux__
  static encoder_t vaapi {
    "vaapi"sv,
    std::make_unique<encoder_platform_formats_avcodec>(
      AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_NONE,
      AV_PIX_FMT_VAAPI,
      AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P10,
      vaapi_init_avcodec_hardware_input_buffer),
    {
      // Common options
      {
        { "async_depth"s, 1 },
        { "idr_interval"s, std::numeric_limits<int>::max() },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "av1_vaapi"s,
    },
    {
      // Common options
      {
        { "async_depth"s, 1 },
        { "sei"s, 0 },
        { "idr_interval"s, std::numeric_limits<int>::max() },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "hevc_vaapi"s,
    },
    {
      // Common options
      {
        { "async_depth"s, 1 },
        { "sei"s, 0 },
        { "idr_interval"s, std::numeric_limits<int>::max() },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "h264_vaapi"s,
    },
    LIMITED_GOP_SIZE | PARALLEL_ENCODING | SINGLE_SLICE_ONLY | NO_RC_BUF_LIMIT
  };
#endif

#ifdef __APPLE__
  static encoder_t videotoolbox {
    "videotoolbox"sv,
    std::make_unique<encoder_platform_formats_avcodec>(
      AV_HWDEVICE_TYPE_VIDEOTOOLBOX, AV_HWDEVICE_TYPE_NONE,
      AV_PIX_FMT_VIDEOTOOLBOX,
      AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
      vt_init_avcodec_hardware_input_buffer),
    {
      // Common options
      {
        { "allow_sw"s, &config::video.vt.vt_allow_sw },
        { "require_sw"s, &config::video.vt.vt_require_sw },
        { "realtime"s, &config::video.vt.vt_realtime },
        { "prio_speed"s, 1 },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::nullopt,
      "av1_videotoolbox"s,
    },
    {
      // Common options
      {
        { "allow_sw"s, &config::video.vt.vt_allow_sw },
        { "require_sw"s, &config::video.vt.vt_require_sw },
        { "realtime"s, &config::video.vt.vt_realtime },
        { "prio_speed"s, 1 },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::nullopt,
      "hevc_videotoolbox"s,
    },
    {
      // Common options
      {
        { "allow_sw"s, &config::video.vt.vt_allow_sw },
        { "require_sw"s, &config::video.vt.vt_require_sw },
        { "realtime"s, &config::video.vt.vt_realtime },
        { "prio_speed"s, 1 },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::nullopt,
      "h264_videotoolbox"s,
    },
    DEFAULT
  };
#endif

  static const std::vector<encoder_t *> encoders {
#ifndef __APPLE__
    &nvenc,
#endif
#ifdef _WIN32
    &quicksync,
    &amdvce,
#endif
#ifdef __linux__
    &vaapi,
#endif
#ifdef __APPLE__
    &videotoolbox,
#endif
    &software
  };

  static encoder_t *chosen_encoder;
  int active_hevc_mode;
  int active_av1_mode;
  bool last_encoder_probe_supported_ref_frames_invalidation = false;

  void
  reset_display(std::shared_ptr<platf::display_t> &disp, const platf::mem_type_e &type, const std::string &display_name, const config_t &config) {
    // We try this twice, in case we still get an error on reinitialization
    for (int x = 0; x < 2; ++x) {
      disp.reset();
      disp = platf::display(type, display_name, config);
      if (disp) {
        break;
      }

      // The capture code depends on us to sleep between failures
      std::this_thread::sleep_for(200ms);
    }
  }

  void
  captureThread(
    std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue,
    sync_util::sync_t<std::weak_ptr<platf::display_t>> &display_wp,
    safe::signal_t &reinit_event,
    const encoder_t &encoder) {
    std::vector<capture_ctx_t> capture_ctxs;

    auto fg = util::fail_guard([&]() {
      capture_ctx_queue->stop();

      // Stop all sessions listening to this thread
      for (auto &capture_ctx : capture_ctxs) {
        capture_ctx.images->stop();
      }
      for (auto &capture_ctx : capture_ctx_queue->unsafe()) {
        capture_ctx.images->stop();
      }
    });

    auto switch_display_event = mail::man->event<int>(mail::switch_display);

    // Get all the monitor names now, rather than at boot, to
    // get the most up-to-date list available monitors
    auto display_names = platf::display_names(encoder.platform_formats->dev_type);
    int display_p = 0;

    if (display_names.empty()) {
      display_names.emplace_back(config::video.output_name);
    }

    for (int x = 0; x < display_names.size(); ++x) {
      if (display_names[x] == config::video.output_name) {
        display_p = x;

        break;
      }
    }

    // Wait for the initial capture context or a request to stop the queue
    auto initial_capture_ctx = capture_ctx_queue->pop();
    if (!initial_capture_ctx) {
      return;
    }
    capture_ctxs.emplace_back(std::move(*initial_capture_ctx));

    auto disp = platf::display(encoder.platform_formats->dev_type, display_names[display_p], capture_ctxs.front().config);
    if (!disp) {
      return;
    }
    display_wp = disp;

    constexpr auto capture_buffer_size = 12;
    std::list<std::shared_ptr<platf::img_t>> imgs(capture_buffer_size);

    std::vector<std::optional<std::chrono::steady_clock::time_point>> imgs_used_timestamps;
    const std::chrono::seconds trim_timeot = 3s;
    auto trim_imgs = [&]() {
      // count allocated and used within current pool
      size_t allocated_count = 0;
      size_t used_count = 0;
      for (const auto &img : imgs) {
        if (img) {
          allocated_count += 1;
          if (img.use_count() > 1) {
            used_count += 1;
          }
        }
      }

      // remember the timestamp of currently used count
      const auto now = std::chrono::steady_clock::now();
      if (imgs_used_timestamps.size() <= used_count) {
        imgs_used_timestamps.resize(used_count + 1);
      }
      imgs_used_timestamps[used_count] = now;

      // decide whether to trim allocated unused above the currently used count
      // based on last used timestamp and universal timeout
      size_t trim_target = used_count;
      for (size_t i = used_count; i < imgs_used_timestamps.size(); i++) {
        if (imgs_used_timestamps[i] && now - *imgs_used_timestamps[i] < trim_timeot) {
          trim_target = i;
        }
      }

      // trim allocated unused above the newly decided trim target
      if (allocated_count > trim_target) {
        size_t to_trim = allocated_count - trim_target;
        // prioritize trimming least recently used
        for (auto it = imgs.rbegin(); it != imgs.rend(); it++) {
          auto &img = *it;
          if (img && img.use_count() == 1) {
            img.reset();
            to_trim -= 1;
            if (to_trim == 0) break;
          }
        }
        // forget timestamps that no longer relevant
        imgs_used_timestamps.resize(trim_target + 1);
      }
    };

    auto pull_free_image_callback = [&](std::shared_ptr<platf::img_t> &img_out) -> bool {
      img_out.reset();
      while (capture_ctx_queue->running()) {
        // pick first allocated but unused
        for (auto it = imgs.begin(); it != imgs.end(); it++) {
          if (*it && it->use_count() == 1) {
            img_out = *it;
            if (it != imgs.begin()) {
              // move image to the front of the list to prioritize its reusal
              imgs.erase(it);
              imgs.push_front(img_out);
            }
            break;
          }
        }
        // otherwise pick first unallocated
        if (!img_out) {
          for (auto it = imgs.begin(); it != imgs.end(); it++) {
            if (!*it) {
              // allocate image
              *it = disp->alloc_img();
              img_out = *it;
              if (it != imgs.begin()) {
                // move image to the front of the list to prioritize its reusal
                imgs.erase(it);
                imgs.push_front(img_out);
              }
              break;
            }
          }
        }
        if (img_out) {
          // trim allocated but unused portion of the pool based on timeouts
          trim_imgs();
          img_out->frame_timestamp.reset();
          return true;
        }
        else {
          // sleep and retry if image pool is full
          std::this_thread::sleep_for(1ms);
        }
      }
      return false;
    };

    // Capture takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::critical);

    while (capture_ctx_queue->running()) {
      bool artificial_reinit = false;

      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        KITTY_WHILE_LOOP(auto capture_ctx = std::begin(capture_ctxs), capture_ctx != std::end(capture_ctxs), {
          if (!capture_ctx->images->running()) {
            capture_ctx = capture_ctxs.erase(capture_ctx);

            continue;
          }

          if (frame_captured) {
            capture_ctx->images->raise(img);
          }

          ++capture_ctx;
        })

        if (!capture_ctx_queue->running()) {
          return false;
        }

        while (capture_ctx_queue->peek()) {
          capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));
        }

        if (switch_display_event->peek()) {
          artificial_reinit = true;

          display_p = std::clamp(*switch_display_event->pop(), 0, (int) display_names.size() - 1);
          return false;
        }

        return true;
      };

      auto status = disp->capture(push_captured_image_callback, pull_free_image_callback, &display_cursor);

      if (artificial_reinit && status != platf::capture_e::error) {
        status = platf::capture_e::reinit;

        artificial_reinit = false;
      }

      switch (status) {
        case platf::capture_e::reinit: {
          reinit_event.raise(true);

          // Some classes of images contain references to the display --> display won't delete unless img is deleted
          for (auto &img : imgs) {
            img.reset();
          }

          // display_wp is modified in this thread only
          // Wait for the other shared_ptr's of display to be destroyed.
          // New displays will only be created in this thread.
          while (display_wp->use_count() != 1) {
            // Free images that weren't consumed by the encoders. These can reference the display and prevent
            // the ref count from reaching 1. We do this here rather than on the encoder thread to avoid race
            // conditions where the encoding loop might free a good frame after reinitializing if we capture
            // a new frame here before the encoder has finished reinitializing.
            KITTY_WHILE_LOOP(auto capture_ctx = std::begin(capture_ctxs), capture_ctx != std::end(capture_ctxs), {
              if (!capture_ctx->images->running()) {
                capture_ctx = capture_ctxs.erase(capture_ctx);
                continue;
              }

              while (capture_ctx->images->peek()) {
                capture_ctx->images->pop();
              }

              ++capture_ctx;
            });

            std::this_thread::sleep_for(20ms);
          }

          while (capture_ctx_queue->running()) {
            // reset_display() will sleep between retries
            reset_display(disp, encoder.platform_formats->dev_type, display_names[display_p], capture_ctxs.front().config);
            if (disp) {
              break;
            }
          }
          if (!disp) {
            return;
          }

          display_wp = disp;

          reinit_event.reset();
          continue;
        }
        case platf::capture_e::error:
        case platf::capture_e::ok:
        case platf::capture_e::timeout:
        case platf::capture_e::interrupted:
          return;
        default:
          BOOST_LOG(error) << "Unrecognized capture status ["sv << (int) status << ']';
          return;
      }
    }
  }

  int
  encode_avcodec(int64_t frame_nr, avcodec_encode_session_t &session, safe::mail_raw_t::queue_t<packet_t> &packets, void *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp) {
    auto &frame = session.device->frame;
    frame->pts = frame_nr;

    auto &ctx = session.avcodec_ctx;

    auto &sps = session.sps;
    auto &vps = session.vps;

    // send the frame to the encoder
    auto ret = avcodec_send_frame(ctx.get(), frame);
    if (ret < 0) {
      char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
      BOOST_LOG(error) << "Could not send a frame for encoding: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, ret);

      return -1;
    }

    while (ret >= 0) {
      auto packet = std::make_unique<packet_raw_avcodec>();
      auto av_packet = packet.get()->av_packet;

      ret = avcodec_receive_packet(ctx.get(), av_packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
      }
      else if (ret < 0) {
        return ret;
      }

      if ((frame->flags & AV_FRAME_FLAG_KEY) && !(av_packet->flags & AV_PKT_FLAG_KEY)) {
        BOOST_LOG(error) << "Encoder did not produce IDR frame when requested!"sv;
      }

      if (session.inject) {
        if (session.inject == 1) {
          auto h264 = cbs::make_sps_h264(ctx.get(), av_packet);

          sps = std::move(h264.sps);
        }
        else {
          auto hevc = cbs::make_sps_hevc(ctx.get(), av_packet);

          sps = std::move(hevc.sps);
          vps = std::move(hevc.vps);

          session.replacements.emplace_back(
            std::string_view((char *) std::begin(vps.old), vps.old.size()),
            std::string_view((char *) std::begin(vps._new), vps._new.size()));
        }

        session.inject = 0;

        session.replacements.emplace_back(
          std::string_view((char *) std::begin(sps.old), sps.old.size()),
          std::string_view((char *) std::begin(sps._new), sps._new.size()));
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

  int
  encode_nvenc(int64_t frame_nr, nvenc_encode_session_t &session, safe::mail_raw_t::queue_t<packet_t> &packets, void *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp) {
    auto encoded_frame = session.encode_frame(frame_nr);
    if (encoded_frame.data.empty()) {
      BOOST_LOG(error) << "NvENC returned empty packet";
      return -1;
    }

    if (frame_nr != encoded_frame.frame_index) {
      BOOST_LOG(error) << "NvENC frame index mismatch " << frame_nr << " " << encoded_frame.frame_index;
    }

    auto packet = std::make_unique<packet_raw_generic>(std::move(encoded_frame.data), encoded_frame.frame_index, encoded_frame.idr);
    packet->channel_data = channel_data;
    packet->after_ref_frame_invalidation = encoded_frame.after_ref_frame_invalidation;
    packet->frame_timestamp = frame_timestamp;
    packets->raise(std::move(packet));

    return 0;
  }

  int
  encode(int64_t frame_nr, encode_session_t &session, safe::mail_raw_t::queue_t<packet_t> &packets, void *channel_data, std::optional<std::chrono::steady_clock::time_point> frame_timestamp) {
    if (auto avcodec_session = dynamic_cast<avcodec_encode_session_t *>(&session)) {
      return encode_avcodec(frame_nr, *avcodec_session, packets, channel_data, frame_timestamp);
    }
    else if (auto nvenc_session = dynamic_cast<nvenc_encode_session_t *>(&session)) {
      return encode_nvenc(frame_nr, *nvenc_session, packets, channel_data, frame_timestamp);
    }

    return -1;
  }

  std::unique_ptr<avcodec_encode_session_t>
  make_avcodec_encode_session(platf::display_t *disp, const encoder_t &encoder, const config_t &config, int width, int height, std::unique_ptr<platf::avcodec_encode_device_t> encode_device) {
    auto platform_formats = dynamic_cast<const encoder_platform_formats_avcodec *>(encoder.platform_formats.get());
    if (!platform_formats) {
      return nullptr;
    }

    bool hardware = platform_formats->avcodec_base_dev_type != AV_HWDEVICE_TYPE_NONE;

    auto &video_format = config.videoFormat == 0 ? encoder.h264 :
                         config.videoFormat == 1 ? encoder.hevc :
                                                   encoder.av1;
    if (!video_format[encoder_t::PASSED] || !disp->is_codec_supported(video_format.name, config)) {
      BOOST_LOG(error) << encoder.name << ": "sv << video_format.name << " mode not supported"sv;
      return nullptr;
    }

    if (config.dynamicRange && !video_format[encoder_t::DYNAMIC_RANGE]) {
      BOOST_LOG(error) << video_format.name << ": dynamic range not supported"sv;
      return nullptr;
    }

    auto codec = avcodec_find_encoder_by_name(video_format.name.c_str());
    if (!codec) {
      BOOST_LOG(error) << "Couldn't open ["sv << video_format.name << ']';

      return nullptr;
    }

    avcodec_ctx_t ctx { avcodec_alloc_context3(codec) };
    ctx->width = config.width;
    ctx->height = config.height;
    ctx->time_base = AVRational { 1, config.framerate };
    ctx->framerate = AVRational { config.framerate, 1 };

    switch (config.videoFormat) {
      case 0:
        ctx->profile = FF_PROFILE_H264_HIGH;
        break;

      case 1:
        ctx->profile = config.dynamicRange ? FF_PROFILE_HEVC_MAIN_10 : FF_PROFILE_HEVC_MAIN;
        break;

      case 2:
        // AV1 supports both 8 and 10 bit encoding with the same Main profile
        ctx->profile = FF_PROFILE_AV1_MAIN;
        break;
    }

    // B-frames delay decoder output, so never use them
    ctx->max_b_frames = 0;

    // Use an infinite GOP length since I-frames are generated on demand
    ctx->gop_size = encoder.flags & LIMITED_GOP_SIZE ?
                      std::numeric_limits<std::int16_t>::max() :
                      std::numeric_limits<int>::max();

    ctx->keyint_min = std::numeric_limits<int>::max();

    // Some client decoders have limits on the number of reference frames
    if (config.numRefFrames) {
      if (video_format[encoder_t::REF_FRAMES_RESTRICT]) {
        ctx->refs = config.numRefFrames;
      }
      else {
        BOOST_LOG(warning) << "Client requested reference frame limit, but encoder doesn't support it!"sv;
      }
    }

    ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
    ctx->flags2 |= AV_CODEC_FLAG2_FAST;

    auto colorspace = encode_device->colorspace;
    auto avcodec_colorspace = avcodec_colorspace_from_sunshine_colorspace(colorspace);

    ctx->color_range = avcodec_colorspace.range;
    ctx->color_primaries = avcodec_colorspace.primaries;
    ctx->color_trc = avcodec_colorspace.transfer_function;
    ctx->colorspace = avcodec_colorspace.matrix;

    auto sw_fmt = (colorspace.bit_depth == 10) ? platform_formats->avcodec_pix_fmt_10bit : platform_formats->avcodec_pix_fmt_8bit;

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
          char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
          BOOST_LOG(error) << "Failed to derive device context: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

          return nullptr;
        }

        encoding_stream_context = std::move(derived_context);
      }

      // Initialize avcodec hardware frames
      {
        avcodec_buffer_t frame_ref { av_hwframe_ctx_alloc(encoding_stream_context.get()) };

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
    }
    else /* software */ {
      ctx->pix_fmt = sw_fmt;

      // Clients will request for the fewest slices per frame to get the
      // most efficient encode, but we may want to provide more slices than
      // requested to ensure we have enough parallelism for good performance.
      ctx->slices = std::max(config.slicesPerFrame, config::video.min_threads);
    }

    if (encoder.flags & SINGLE_SLICE_ONLY) {
      ctx->slices = 1;
    }

    ctx->thread_type = FF_THREAD_SLICE;
    ctx->thread_count = ctx->slices;

    AVDictionary *options { nullptr };
    auto handle_option = [&options](const encoder_t::option_t &option) {
      std::visit(
        util::overloaded {
          [&](int v) { av_dict_set_int(&options, option.name.c_str(), v, 0); },
          [&](int *v) { av_dict_set_int(&options, option.name.c_str(), *v, 0); },
          [&](std::optional<int> *v) { if(*v) av_dict_set_int(&options, option.name.c_str(), **v, 0); },
          [&](std::function<int()> v) { av_dict_set_int(&options, option.name.c_str(), v(), 0); },
          [&](const std::string &v) { av_dict_set(&options, option.name.c_str(), v.c_str(), 0); },
          [&](std::string *v) { if(!v->empty()) av_dict_set(&options, option.name.c_str(), v->c_str(), 0); } },
        option.value);
    };

    // Apply common options, then format-specific overrides
    for (auto &option : video_format.common_options) {
      handle_option(option);
    }
    for (auto &option : (config.dynamicRange ? video_format.hdr_options : video_format.sdr_options)) {
      handle_option(option);
    }

    if (video_format[encoder_t::CBR]) {
      auto bitrate = config.bitrate * 1000;
      ctx->rc_max_rate = bitrate;
      ctx->bit_rate = bitrate;

      if (encoder.flags & CBR_WITH_VBR) {
        // Ensure rc_max_bitrate != bit_rate to force VBR mode
        ctx->bit_rate--;
      }
      else {
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
        }
        else {
          ctx->rc_buffer_size = bitrate / config.framerate;
        }
      }
    }
    else if (video_format.qp) {
      handle_option(*video_format.qp);
    }
    else {
      BOOST_LOG(error) << "Couldn't set video quality: encoder "sv << encoder.name << " doesn't support qp"sv;
      return nullptr;
    }

    if (auto status = avcodec_open2(ctx.get(), codec, &options)) {
      char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
      BOOST_LOG(error)
        << "Could not open codec ["sv
        << video_format.name << "]: "sv
        << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, status);

      return nullptr;
    }

    avcodec_frame_t frame { av_frame_alloc() };
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
      }
      else {
        BOOST_LOG(error) << "Couldn't get display hdr metadata when colorspace selection indicates it should have one";
      }
    }

    std::unique_ptr<platf::avcodec_encode_device_t> encode_device_final;

    if (!encode_device->data) {
      auto software_encode_device = std::make_unique<avcodec_software_encode_device_t>();

      if (software_encode_device->init(width, height, frame.get(), sw_fmt, hardware)) {
        return nullptr;
      }
      software_encode_device->colorspace = colorspace;

      encode_device_final = std::move(software_encode_device);
    }
    else {
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
      config.videoFormat <= 1 ? (1 - (int) video_format[encoder_t::VUI_PARAMETERS]) * (1 + config.videoFormat) : 0);

    return session;
  }

  std::unique_ptr<nvenc_encode_session_t>
  make_nvenc_encode_session(const config_t &client_config, std::unique_ptr<platf::nvenc_encode_device_t> encode_device) {
    if (!encode_device->init_encoder(client_config, encode_device->colorspace)) {
      return nullptr;
    }

    return std::make_unique<nvenc_encode_session_t>(std::move(encode_device));
  }

  std::unique_ptr<encode_session_t>
  make_encode_session(platf::display_t *disp, const encoder_t &encoder, const config_t &config, int width, int height, std::unique_ptr<platf::encode_device_t> encode_device) {
    if (encode_device) {
      switch (encode_device->colorspace.colorspace) {
        case colorspace_e::bt2020:
          BOOST_LOG(info) << "HDR color coding [Rec. 2020 + SMPTE 2084 PQ]"sv;
          break;

        case colorspace_e::rec601:
          BOOST_LOG(info) << "SDR color coding [Rec. 601]"sv;
          break;

        case colorspace_e::rec709:
          BOOST_LOG(info) << "SDR color coding [Rec. 709]"sv;
          break;

        case colorspace_e::bt2020sdr:
          BOOST_LOG(info) << "SDR color coding [Rec. 2020]"sv;
          break;
      }
      BOOST_LOG(info) << "Color depth: " << encode_device->colorspace.bit_depth << "-bit";
      BOOST_LOG(info) << "Color range: ["sv << (encode_device->colorspace.full_range ? "JPEG"sv : "MPEG"sv) << ']';
    }

    if (dynamic_cast<platf::avcodec_encode_device_t *>(encode_device.get())) {
      auto avcodec_encode_device = boost::dynamic_pointer_cast<platf::avcodec_encode_device_t>(std::move(encode_device));
      return make_avcodec_encode_session(disp, encoder, config, width, height, std::move(avcodec_encode_device));
    }
    else if (dynamic_cast<platf::nvenc_encode_device_t *>(encode_device.get())) {
      auto nvenc_encode_device = boost::dynamic_pointer_cast<platf::nvenc_encode_device_t>(std::move(encode_device));
      return make_nvenc_encode_session(config, std::move(nvenc_encode_device));
    }

    return nullptr;
  }

  void
  encode_run(
    int &frame_nr,  // Store progress of the frame number
    safe::mail_t mail,
    img_event_t images,
    config_t config,
    std::shared_ptr<platf::display_t> disp,
    std::unique_ptr<platf::encode_device_t> encode_device,
    safe::signal_t &reinit_event,
    const encoder_t &encoder,
    void *channel_data) {
    auto session = make_encode_session(disp.get(), encoder, config, disp->width, disp->height, std::move(encode_device));
    if (!session) {
      return;
    }

    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = mail::man->queue<packet_t>(mail::video_packets);
    auto idr_events = mail->event<bool>(mail::idr);
    auto invalidate_ref_frames_events = mail->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames);

    {
      // Load a dummy image into the AVFrame to ensure we have something to encode
      // even if we timeout waiting on the first frame. This is a relatively large
      // allocation which can be freed immediately after convert(), so we do this
      // in a separate scope.
      auto dummy_img = disp->alloc_img();
      if (!dummy_img || disp->dummy_img(dummy_img.get()) || session->convert(*dummy_img)) {
        return;
      }
    }

    while (true) {
      if (shutdown_event->peek() || reinit_event.peek() || !images->running()) {
        break;
      }

      bool requested_idr_frame = false;

      while (invalidate_ref_frames_events->peek()) {
        if (auto frames = invalidate_ref_frames_events->pop(0ms)) {
          session->invalidate_ref_frames(frames->first, frames->second);
        }
      }

      if (idr_events->peek()) {
        requested_idr_frame = true;
        idr_events->pop();
      }

      if (requested_idr_frame) {
        session->request_idr_frame();
      }

      std::optional<std::chrono::steady_clock::time_point> frame_timestamp;

      // Encode at a minimum of 10 FPS to avoid image quality issues with static content
      if (!requested_idr_frame || images->peek()) {
        if (auto img = images->pop(100ms)) {
          frame_timestamp = img->frame_timestamp;
          if (session->convert(*img)) {
            BOOST_LOG(error) << "Could not convert image"sv;
            return;
          }
        }
        else if (!images->running()) {
          break;
        }
      }

      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {
        BOOST_LOG(error) << "Could not encode video packet"sv;
        return;
      }

      session->request_normal_frame();
    }
  }

  input::touch_port_t
  make_port(platf::display_t *display, const config_t &config) {
    float wd = display->width;
    float hd = display->height;

    float wt = config.width;
    float ht = config.height;

    auto scalar = std::fminf(wt / wd, ht / hd);

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
      },
      display->env_width,
      display->env_height,
      offsetX,
      offsetY,
      1.0f / scalar,
    };
  }

  std::unique_ptr<platf::encode_device_t>
  make_encode_device(platf::display_t &disp, const encoder_t &encoder, const config_t &config) {
    std::unique_ptr<platf::encode_device_t> result;

    auto colorspace = colorspace_from_client_config(config, disp.is_hdr());
    auto pix_fmt = (colorspace.bit_depth == 10) ? encoder.platform_formats->pix_fmt_10bit : encoder.platform_formats->pix_fmt_8bit;

    if (dynamic_cast<const encoder_platform_formats_avcodec *>(encoder.platform_formats.get())) {
      result = disp.make_avcodec_encode_device(pix_fmt);
    }
    else if (dynamic_cast<const encoder_platform_formats_nvenc *>(encoder.platform_formats.get())) {
      result = disp.make_nvenc_encode_device(pix_fmt);
    }

    if (result) {
      result->colorspace = colorspace;
    }

    return result;
  }

  std::optional<sync_session_t>
  make_synced_session(platf::display_t *disp, const encoder_t &encoder, platf::img_t &img, sync_session_ctx_t &ctx) {
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
      }
      else {
        BOOST_LOG(error) << "Couldn't get display hdr metadata when colorspace selection indicates it should have one";
      }
    }
    ctx.hdr_events->raise(std::move(hdr_info));

    auto session = make_encode_session(disp, encoder, ctx.config, img.width, img.height, std::move(encode_device));
    if (!session) {
      return std::nullopt;
    }

    // Load the initial image to prepare for encoding
    if (session->convert(img)) {
      BOOST_LOG(error) << "Could not convert initial image"sv;
      return std::nullopt;
    }

    encode_session.session = std::move(session);

    return encode_session;
  }

  encode_e
  encode_run_sync(
    std::vector<std::unique_ptr<sync_session_ctx_t>> &synced_session_ctxs,
    encode_session_ctx_queue_t &encode_session_ctx_queue) {
    const auto &encoder = *chosen_encoder;
    auto display_names = platf::display_names(encoder.platform_formats->dev_type);
    int display_p = 0;

    if (display_names.empty()) {
      display_names.emplace_back(config::video.output_name);
    }

    for (int x = 0; x < display_names.size(); ++x) {
      if (display_names[x] == config::video.output_name) {
        display_p = x;

        break;
      }
    }

    std::shared_ptr<platf::display_t> disp;

    auto switch_display_event = mail::man->event<int>(mail::switch_display);

    if (synced_session_ctxs.empty()) {
      auto ctx = encode_session_ctx_queue.pop();
      if (!ctx) {
        return encode_e::ok;
      }

      synced_session_ctxs.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*ctx)));
    }

    while (encode_session_ctx_queue.running()) {
      // reset_display() will sleep between retries
      reset_display(disp, encoder.platform_formats->dev_type, display_names[display_p], synced_session_ctxs.front()->config);
      if (disp) {
        break;
      }
    }

    if (!disp) {
      return encode_e::error;
    }

    auto img = disp->alloc_img();
    if (!img || disp->dummy_img(img.get())) {
      return encode_e::error;
    }

    std::vector<sync_session_t> synced_sessions;
    for (auto &ctx : synced_session_ctxs) {
      auto synced_session = make_synced_session(disp.get(), encoder, *img, *ctx);
      if (!synced_session) {
        return encode_e::error;
      }

      synced_sessions.emplace_back(std::move(*synced_session));
    }

    auto ec = platf::capture_e::ok;
    while (encode_session_ctx_queue.running()) {
      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        while (encode_session_ctx_queue.peek()) {
          auto encode_session_ctx = encode_session_ctx_queue.pop();
          if (!encode_session_ctx) {
            return false;
          }

          synced_session_ctxs.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*encode_session_ctx)));

          auto encode_session = make_synced_session(disp.get(), encoder, *img, *synced_session_ctxs.back());
          if (!encode_session) {
            ec = platf::capture_e::error;
            return false;
          }

          synced_sessions.emplace_back(std::move(*encode_session));
        }

        KITTY_WHILE_LOOP(auto pos = std::begin(synced_sessions), pos != std::end(synced_sessions), {
          auto ctx = pos->ctx;
          if (ctx->shutdown_event->peek()) {
            // Let waiting thread know it can delete shutdown_event
            ctx->join_event->raise(true);

            pos = synced_sessions.erase(pos);
            synced_session_ctxs.erase(std::find_if(std::begin(synced_session_ctxs), std::end(synced_session_ctxs), [&ctx_p = ctx](auto &ctx) {
              return ctx.get() == ctx_p;
            }));

            if (synced_sessions.empty()) {
              return false;
            }

            continue;
          }

          if (ctx->idr_events->peek()) {
            pos->session->request_idr_frame();
            ctx->idr_events->pop();
          }

          if (frame_captured && pos->session->convert(*img)) {
            BOOST_LOG(error) << "Could not convert image"sv;
            ctx->shutdown_event->raise(true);

            continue;
          }

          std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
          if (img) {
            frame_timestamp = img->frame_timestamp;
          }

          if (encode(ctx->frame_nr++, *pos->session, ctx->packets, ctx->channel_data, frame_timestamp)) {
            BOOST_LOG(error) << "Could not encode video packet"sv;
            ctx->shutdown_event->raise(true);

            continue;
          }

          pos->session->request_normal_frame();

          ++pos;
        })

        if (switch_display_event->peek()) {
          ec = platf::capture_e::reinit;

          display_p = std::clamp(*switch_display_event->pop(), 0, (int) display_names.size() - 1);
          return false;
        }

        return true;
      };

      auto pull_free_image_callback = [&img](std::shared_ptr<platf::img_t> &img_out) -> bool {
        img_out = img;
        img_out->frame_timestamp.reset();
        return true;
      };

      auto status = disp->capture(push_captured_image_callback, pull_free_image_callback, &display_cursor);
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

  void
  captureThreadSync() {
    auto ref = capture_thread_sync.ref();

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
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    while (encode_run_sync(synced_session_ctxs, ctx) == encode_e::reinit) {}
  }

  void
  capture_async(
    safe::mail_t mail,
    config_t &config,
    void *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);

    auto images = std::make_shared<img_event_t::element_type>();
    auto lg = util::fail_guard([&]() {
      images->stop();
      shutdown_event->raise(true);
    });

    auto ref = capture_thread_async.ref();
    if (!ref) {
      return;
    }

    ref->capture_ctx_queue->raise(capture_ctx_t { images, config });

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

      auto &encoder = *chosen_encoder;

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
        }
        else {
          BOOST_LOG(error) << "Couldn't get display hdr metadata when colorspace selection indicates it should have one";
        }
      }
      hdr_event->raise(std::move(hdr_info));

      encode_run(
        frame_nr,
        mail, images,
        config, display,
        std::move(encode_device),
        ref->reinit_event, *ref->encoder_p,
        channel_data);
    }
  }

  void
  capture(
    safe::mail_t mail,
    config_t config,
    void *channel_data) {
    auto idr_events = mail->event<bool>(mail::idr);

    idr_events->raise(true);
    if (chosen_encoder->flags & PARALLEL_ENCODING) {
      capture_async(std::move(mail), config, channel_data);
    }
    else {
      safe::signal_t join_event;
      auto ref = capture_thread_sync.ref();
      ref->encode_session_ctx_queue.raise(sync_session_ctx_t {
        &join_event,
        mail->event<bool>(mail::shutdown),
        mail::man->queue<packet_t>(mail::video_packets),
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

  enum validate_flag_e {
    VUI_PARAMS = 0x01,
  };

  int
  validate_config(std::shared_ptr<platf::display_t> &disp, const encoder_t &encoder, const config_t &config) {
    reset_display(disp, encoder.platform_formats->dev_type, config::video.output_name, config);
    if (!disp) {
      return -1;
    }

    auto encode_device = make_encode_device(*disp, encoder, config);
    if (!encode_device) {
      return -1;
    }

    auto session = make_encode_session(disp.get(), encoder, config, disp->width, disp->height, std::move(encode_device));
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

    auto packets = mail::man->queue<packet_t>(mail::video_packets);
    while (!packets->peek()) {
      if (encode(1, *session, packets, nullptr, {})) {
        return -1;
      }
    }

    auto packet = packets->pop();
    if (!packet->is_idr()) {
      BOOST_LOG(error) << "First packet type is not an IDR frame"sv;

      return -1;
    }

    int flag = 0;

    // This check only applies for H.264 and HEVC
    if (config.videoFormat <= 1) {
      if (auto packet_avcodec = dynamic_cast<packet_raw_avcodec *>(packet.get())) {
        if (cbs::validate_sps(packet_avcodec->av_packet, config.videoFormat ? AV_CODEC_ID_H265 : AV_CODEC_ID_H264)) {
          flag |= VUI_PARAMS;
        }
      }
      else {
        // Don't check it for non-avcodec encoders.
        flag |= VUI_PARAMS;
      }
    }

    return flag;
  }

  bool
  validate_encoder(encoder_t &encoder, bool expect_failure) {
    std::shared_ptr<platf::display_t> disp;

    BOOST_LOG(info) << "Trying encoder ["sv << encoder.name << ']';
    auto fg = util::fail_guard([&]() {
      BOOST_LOG(info) << "Encoder ["sv << encoder.name << "] failed"sv;
    });

    auto test_hevc = active_hevc_mode >= 2 || (active_hevc_mode == 0 && !(encoder.flags & H264_ONLY));
    auto test_av1 = active_av1_mode >= 2 || (active_av1_mode == 0 && !(encoder.flags & H264_ONLY));

    encoder.h264.capabilities.set();
    encoder.hevc.capabilities.set();
    encoder.av1.capabilities.set();

    // First, test encoder viability
    config_t config_max_ref_frames { 1920, 1080, 60, 1000, 1, 1, 1, 0, 0 };
    config_t config_autoselect { 1920, 1080, 60, 1000, 1, 0, 1, 0, 0 };

    // If the encoder isn't supported at all (not even H.264), bail early
    reset_display(disp, encoder.platform_formats->dev_type, config::video.output_name, config_autoselect);
    if (!disp) {
      return false;
    }
    if (!disp->is_codec_supported(encoder.h264.name, config_autoselect)) {
      fg.disable();
      BOOST_LOG(info) << "Encoder ["sv << encoder.name << "] is not supported on this GPU"sv;
      return false;
    }

  retry:
    // If we're expecting failure, use the autoselect ref config first since that will always succeed
    // if the encoder is available.
    auto max_ref_frames_h264 = expect_failure ? -1 : validate_config(disp, encoder, config_max_ref_frames);
    auto autoselect_h264 = max_ref_frames_h264 >= 0 ? max_ref_frames_h264 : validate_config(disp, encoder, config_autoselect);
    if (autoselect_h264 < 0) {
      if (encoder.h264.qp && encoder.h264[encoder_t::CBR]) {
        // It's possible the encoder isn't accepting Constant Bit Rate. Turn off CBR and make another attempt
        encoder.h264.capabilities.set();
        encoder.h264[encoder_t::CBR] = false;
        goto retry;
      }
      return false;
    }
    else if (expect_failure) {
      // We expected failure, but actually succeeded. Do the max_ref_frames probe we skipped.
      max_ref_frames_h264 = validate_config(disp, encoder, config_max_ref_frames);
    }

    std::vector<std::pair<validate_flag_e, encoder_t::flag_e>> packet_deficiencies {
      { VUI_PARAMS, encoder_t::VUI_PARAMETERS },
    };

    for (auto [validate_flag, encoder_flag] : packet_deficiencies) {
      encoder.h264[encoder_flag] = (max_ref_frames_h264 & validate_flag && autoselect_h264 & validate_flag);
    }

    encoder.h264[encoder_t::REF_FRAMES_RESTRICT] = max_ref_frames_h264 >= 0;
    encoder.h264[encoder_t::PASSED] = true;

    if (test_hevc) {
      config_max_ref_frames.videoFormat = 1;
      config_autoselect.videoFormat = 1;

      if (disp->is_codec_supported(encoder.hevc.name, config_autoselect)) {
      retry_hevc:
        auto max_ref_frames_hevc = validate_config(disp, encoder, config_max_ref_frames);
        auto autoselect_hevc = max_ref_frames_hevc >= 0 ? max_ref_frames_hevc : validate_config(disp, encoder, config_autoselect);
        if (autoselect_hevc < 0 && encoder.hevc.qp && encoder.hevc[encoder_t::CBR]) {
          // It's possible the encoder isn't accepting Constant Bit Rate. Turn off CBR and make another attempt
          encoder.hevc.capabilities.set();
          encoder.hevc[encoder_t::CBR] = false;
          goto retry_hevc;
        }

        for (auto [validate_flag, encoder_flag] : packet_deficiencies) {
          encoder.hevc[encoder_flag] = (max_ref_frames_hevc & validate_flag && autoselect_hevc & validate_flag);
        }

        encoder.hevc[encoder_t::REF_FRAMES_RESTRICT] = max_ref_frames_hevc >= 0;
        encoder.hevc[encoder_t::PASSED] = max_ref_frames_hevc >= 0 || autoselect_hevc >= 0;
      }
      else {
        BOOST_LOG(info) << "Encoder ["sv << encoder.hevc.name << "] is not supported on this GPU"sv;
        encoder.hevc.capabilities.reset();
      }
    }
    else {
      // Clear all cap bits for HEVC if we didn't probe it
      encoder.hevc.capabilities.reset();
    }

    if (test_av1) {
      config_max_ref_frames.videoFormat = 2;
      config_autoselect.videoFormat = 2;

      if (disp->is_codec_supported(encoder.av1.name, config_autoselect)) {
      retry_av1:
        auto max_ref_frames_av1 = validate_config(disp, encoder, config_max_ref_frames);
        auto autoselect_av1 = max_ref_frames_av1 >= 0 ? max_ref_frames_av1 : validate_config(disp, encoder, config_autoselect);
        if (autoselect_av1 < 0 && encoder.av1.qp && encoder.av1[encoder_t::CBR]) {
          // It's possible the encoder isn't accepting Constant Bit Rate. Turn off CBR and make another attempt
          encoder.av1.capabilities.set();
          encoder.av1[encoder_t::CBR] = false;
          goto retry_av1;
        }

        for (auto [validate_flag, encoder_flag] : packet_deficiencies) {
          encoder.av1[encoder_flag] = (max_ref_frames_av1 & validate_flag && autoselect_av1 & validate_flag);
        }

        encoder.av1[encoder_t::REF_FRAMES_RESTRICT] = max_ref_frames_av1 >= 0;
        encoder.av1[encoder_t::PASSED] = max_ref_frames_av1 >= 0 || autoselect_av1 >= 0;
      }
      else {
        BOOST_LOG(info) << "Encoder ["sv << encoder.av1.name << "] is not supported on this GPU"sv;
        encoder.av1.capabilities.reset();
      }
    }
    else {
      // Clear all cap bits for AV1 if we didn't probe it
      encoder.av1.capabilities.reset();
    }

    std::vector<std::pair<encoder_t::flag_e, config_t>> configs {
      { encoder_t::DYNAMIC_RANGE, { 1920, 1080, 60, 1000, 1, 0, 3, 1, 1 } },
    };

    for (auto &[flag, config] : configs) {
      auto h264 = config;
      auto hevc = config;
      auto av1 = config;

      h264.videoFormat = 0;
      hevc.videoFormat = 1;
      av1.videoFormat = 2;

      // HDR is not supported with H.264. Don't bother even trying it.
      encoder.h264[flag] = flag != encoder_t::DYNAMIC_RANGE && validate_config(disp, encoder, h264) >= 0;

      if (encoder.hevc[encoder_t::PASSED]) {
        encoder.hevc[flag] = validate_config(disp, encoder, hevc) >= 0;
      }

      if (encoder.av1[encoder_t::PASSED]) {
        encoder.av1[flag] = validate_config(disp, encoder, av1) >= 0;
      }
    }

    encoder.h264[encoder_t::VUI_PARAMETERS] = encoder.h264[encoder_t::VUI_PARAMETERS] && !config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];
    encoder.hevc[encoder_t::VUI_PARAMETERS] = encoder.hevc[encoder_t::VUI_PARAMETERS] && !config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];

    if (!encoder.h264[encoder_t::VUI_PARAMETERS]) {
      BOOST_LOG(warning) << encoder.name << ": h264 missing sps->vui parameters"sv;
    }
    if (encoder.hevc[encoder_t::PASSED] && !encoder.hevc[encoder_t::VUI_PARAMETERS]) {
      BOOST_LOG(warning) << encoder.name << ": hevc missing sps->vui parameters"sv;
    }

    fg.disable();
    return true;
  }

  /**
   * This is called once at startup and each time a stream is launched to
   * ensure the best encoder is selected. Encoder availability can change
   * at runtime due to all sorts of things from driver updates to eGPUs.
   *
   * This is only safe to call when there is no client actively streaming.
   */
  int
  probe_encoders() {
    auto encoder_list = encoders;

    // Restart encoder selection
    auto previous_encoder = chosen_encoder;
    chosen_encoder = nullptr;
    active_hevc_mode = config::video.hevc_mode;
    active_av1_mode = config::video.av1_mode;
    last_encoder_probe_supported_ref_frames_invalidation = false;

    auto adjust_encoder_constraints = [&](encoder_t *encoder) {
      // If we can't satisfy both the encoder and codec requirement, prefer the encoder over codec support
      if (active_hevc_mode == 3 && !encoder->hevc[encoder_t::DYNAMIC_RANGE]) {
        BOOST_LOG(warning) << "Encoder ["sv << encoder->name << "] does not support HEVC Main10 on this system"sv;
        active_hevc_mode = 0;
      }
      else if (active_hevc_mode == 2 && !encoder->hevc[encoder_t::PASSED]) {
        BOOST_LOG(warning) << "Encoder ["sv << encoder->name << "] does not support HEVC on this system"sv;
        active_hevc_mode = 0;
      }

      if (active_av1_mode == 3 && !encoder->av1[encoder_t::DYNAMIC_RANGE]) {
        BOOST_LOG(warning) << "Encoder ["sv << encoder->name << "] does not support AV1 Main10 on this system"sv;
        active_av1_mode = 0;
      }
      else if (active_av1_mode == 2 && !encoder->av1[encoder_t::PASSED]) {
        BOOST_LOG(warning) << "Encoder ["sv << encoder->name << "] does not support AV1 on this system"sv;
        active_av1_mode = 0;
      }
    };

    if (!config::video.encoder.empty()) {
      // If there is a specific encoder specified, use it if it passes validation
      KITTY_WHILE_LOOP(auto pos = std::begin(encoder_list), pos != std::end(encoder_list), {
        auto encoder = *pos;

        if (encoder->name == config::video.encoder) {
          // Remove the encoder from the list entirely if it fails validation
          if (!validate_encoder(*encoder, previous_encoder && previous_encoder != encoder)) {
            pos = encoder_list.erase(pos);
            break;
          }

          // We will return an encoder here even if it fails one of the codec requirements specified by the user
          adjust_encoder_constraints(encoder);

          chosen_encoder = encoder;
          break;
        }

        pos++;
      });

      if (chosen_encoder == nullptr) {
        BOOST_LOG(error) << "Couldn't find any working encoder matching ["sv << config::video.encoder << ']';
      }
    }

    BOOST_LOG(info) << "// Testing for available encoders, this may generate errors. You can safely ignore those errors. //"sv;

    // If we haven't found an encoder yet, but we want one with specific codec support, search for that now.
    if (chosen_encoder == nullptr && (active_hevc_mode >= 2 || active_av1_mode >= 2)) {
      KITTY_WHILE_LOOP(auto pos = std::begin(encoder_list), pos != std::end(encoder_list), {
        auto encoder = *pos;

        // Remove the encoder from the list entirely if it fails validation
        if (!validate_encoder(*encoder, previous_encoder && previous_encoder != encoder)) {
          pos = encoder_list.erase(pos);
          continue;
        }

        // Skip it if it doesn't support the specified codec at all
        if ((active_hevc_mode >= 2 && !encoder->hevc[encoder_t::PASSED]) ||
            (active_av1_mode >= 2 && !encoder->av1[encoder_t::PASSED])) {
          pos++;
          continue;
        }

        // Skip it if it doesn't support HDR on the specified codec
        if ((active_hevc_mode == 3 && !encoder->hevc[encoder_t::DYNAMIC_RANGE]) ||
            (active_av1_mode == 3 && !encoder->av1[encoder_t::DYNAMIC_RANGE])) {
          pos++;
          continue;
        }

        chosen_encoder = encoder;
        break;
      });

      if (chosen_encoder == nullptr) {
        BOOST_LOG(error) << "Couldn't find any working encoder that meets HEVC/AV1 requirements"sv;
      }
    }

    // If no encoder was specified or the specified encoder was unusable, keep trying
    // the remaining encoders until we find one that passes validation.
    if (chosen_encoder == nullptr) {
      KITTY_WHILE_LOOP(auto pos = std::begin(encoder_list), pos != std::end(encoder_list), {
        auto encoder = *pos;

        // If we've used a previous encoder and it's not this one, we expect this encoder to
        // fail to validate. It will use a slightly different order of checks to more quickly
        // eliminate failing encoders.
        if (!validate_encoder(*encoder, previous_encoder && previous_encoder != encoder)) {
          pos = encoder_list.erase(pos);
          continue;
        }

        // We will return an encoder here even if it fails one of the codec requirements specified by the user
        adjust_encoder_constraints(encoder);

        chosen_encoder = encoder;
        break;
      });
    }

    if (chosen_encoder == nullptr) {
      BOOST_LOG(fatal) << "Couldn't find any working encoder"sv;
      return -1;
    }

    BOOST_LOG(info);
    BOOST_LOG(info) << "// Ignore any errors mentioned above, they are not relevant. //"sv;
    BOOST_LOG(info);

    auto &encoder = *chosen_encoder;

    last_encoder_probe_supported_ref_frames_invalidation = (encoder.flags & REF_FRAMES_INVALIDATION);

    BOOST_LOG(debug) << "------  h264 ------"sv;
    for (int x = 0; x < encoder_t::MAX_FLAGS; ++x) {
      auto flag = (encoder_t::flag_e) x;
      BOOST_LOG(debug) << encoder_t::from_flag(flag) << (encoder.h264[flag] ? ": supported"sv : ": unsupported"sv);
    }
    BOOST_LOG(debug) << "-------------------"sv;
    BOOST_LOG(info) << "Found H.264 encoder: "sv << encoder.h264.name << " ["sv << encoder.name << ']';

    if (encoder.hevc[encoder_t::PASSED]) {
      BOOST_LOG(debug) << "------  hevc ------"sv;
      for (int x = 0; x < encoder_t::MAX_FLAGS; ++x) {
        auto flag = (encoder_t::flag_e) x;
        BOOST_LOG(debug) << encoder_t::from_flag(flag) << (encoder.hevc[flag] ? ": supported"sv : ": unsupported"sv);
      }
      BOOST_LOG(debug) << "-------------------"sv;

      BOOST_LOG(info) << "Found HEVC encoder: "sv << encoder.hevc.name << " ["sv << encoder.name << ']';
    }

    if (encoder.av1[encoder_t::PASSED]) {
      BOOST_LOG(debug) << "------  av1 ------"sv;
      for (int x = 0; x < encoder_t::MAX_FLAGS; ++x) {
        auto flag = (encoder_t::flag_e) x;
        BOOST_LOG(debug) << encoder_t::from_flag(flag) << (encoder.av1[flag] ? ": supported"sv : ": unsupported"sv);
      }
      BOOST_LOG(debug) << "-------------------"sv;

      BOOST_LOG(info) << "Found AV1 encoder: "sv << encoder.av1.name << " ["sv << encoder.name << ']';
    }

    if (active_hevc_mode == 0) {
      active_hevc_mode = encoder.hevc[encoder_t::PASSED] ? (encoder.hevc[encoder_t::DYNAMIC_RANGE] ? 3 : 2) : 1;
    }

    if (active_av1_mode == 0) {
      active_av1_mode = encoder.av1[encoder_t::PASSED] ? (encoder.av1[encoder_t::DYNAMIC_RANGE] ? 3 : 2) : 1;
    }

    return 0;
  }

  // Linux only declaration
  typedef int (*vaapi_init_avcodec_hardware_input_buffer_fn)(platf::avcodec_encode_device_t *encode_device, AVBufferRef **hw_device_buf);

  util::Either<avcodec_buffer_t, int>
  vaapi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device) {
    avcodec_buffer_t hw_device_buf;

    // If an egl hwdevice
    if (encode_device->data) {
      if (((vaapi_init_avcodec_hardware_input_buffer_fn) encode_device->data)(encode_device, &hw_device_buf)) {
        return -1;
      }

      return hw_device_buf;
    }

    auto render_device = config::video.adapter_name.empty() ? nullptr : config::video.adapter_name.c_str();

    auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VAAPI, render_device, nullptr, 0);
    if (status < 0) {
      char string[AV_ERROR_MAX_STRING_SIZE];
      BOOST_LOG(error) << "Failed to create a VAAPI device: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
      return -1;
    }

    return hw_device_buf;
  }

  util::Either<avcodec_buffer_t, int>
  cuda_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device) {
    avcodec_buffer_t hw_device_buf;

    auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 1 /* AV_CUDA_USE_PRIMARY_CONTEXT */);
    if (status < 0) {
      char string[AV_ERROR_MAX_STRING_SIZE];
      BOOST_LOG(error) << "Failed to create a CUDA device: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
      return -1;
    }

    return hw_device_buf;
  }

  util::Either<avcodec_buffer_t, int>
  vt_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device) {
    avcodec_buffer_t hw_device_buf;

    auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    if (status < 0) {
      char string[AV_ERROR_MAX_STRING_SIZE];
      BOOST_LOG(error) << "Failed to create a VideoToolbox device: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
      return -1;
    }

    return hw_device_buf;
  }

#ifdef _WIN32
}

void
do_nothing(void *) {}

namespace video {
  util::Either<avcodec_buffer_t, int>
  dxgi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device) {
    avcodec_buffer_t ctx_buf { av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA) };
    auto ctx = (AVD3D11VADeviceContext *) ((AVHWDeviceContext *) ctx_buf->data)->hwctx;

    std::fill_n((std::uint8_t *) ctx, sizeof(AVD3D11VADeviceContext), 0);

    auto device = (ID3D11Device *) encode_device->data;

    device->AddRef();
    ctx->device = device;

    ctx->lock_ctx = (void *) 1;
    ctx->lock = do_nothing;
    ctx->unlock = do_nothing;

    auto err = av_hwdevice_ctx_init(ctx_buf.get());
    if (err) {
      char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
      BOOST_LOG(error) << "Failed to create FFMpeg hardware device context: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

      return err;
    }

    return ctx_buf;
  }
#endif

  int
  start_capture_async(capture_thread_async_ctx_t &capture_thread_ctx) {
    capture_thread_ctx.encoder_p = chosen_encoder;
    capture_thread_ctx.reinit_event.reset();

    capture_thread_ctx.capture_ctx_queue = std::make_shared<safe::queue_t<capture_ctx_t>>(30);

    capture_thread_ctx.capture_thread = std::thread {
      captureThread,
      capture_thread_ctx.capture_ctx_queue,
      std::ref(capture_thread_ctx.display_wp),
      std::ref(capture_thread_ctx.reinit_event),
      std::ref(*capture_thread_ctx.encoder_p)
    };

    return 0;
  }
  void
  end_capture_async(capture_thread_async_ctx_t &capture_thread_ctx) {
    capture_thread_ctx.capture_ctx_queue->stop();

    capture_thread_ctx.capture_thread.join();
  }

  int
  start_capture_sync(capture_thread_sync_ctx_t &ctx) {
    std::thread { &captureThreadSync }.detach();
    return 0;
  }
  void
  end_capture_sync(capture_thread_sync_ctx_t &ctx) {}

  platf::mem_type_e
  map_base_dev_type(AVHWDeviceType type) {
    switch (type) {
      case AV_HWDEVICE_TYPE_D3D11VA:
        return platf::mem_type_e::dxgi;
      case AV_HWDEVICE_TYPE_VAAPI:
        return platf::mem_type_e::vaapi;
      case AV_HWDEVICE_TYPE_CUDA:
        return platf::mem_type_e::cuda;
      case AV_HWDEVICE_TYPE_NONE:
        return platf::mem_type_e::system;
      case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
        return platf::mem_type_e::videotoolbox;
      default:
        return platf::mem_type_e::unknown;
    }

    return platf::mem_type_e::unknown;
  }

  platf::pix_fmt_e
  map_pix_fmt(AVPixelFormat fmt) {
    switch (fmt) {
      case AV_PIX_FMT_YUV420P10:
        return platf::pix_fmt_e::yuv420p10;
      case AV_PIX_FMT_YUV420P:
        return platf::pix_fmt_e::yuv420p;
      case AV_PIX_FMT_NV12:
        return platf::pix_fmt_e::nv12;
      case AV_PIX_FMT_P010:
        return platf::pix_fmt_e::p010;
      default:
        return platf::pix_fmt_e::unknown;
    }

    return platf::pix_fmt_e::unknown;
  }

}  // namespace video
