//
// Created by loki on 6/6/19.
//

#include <atomic>
#include <bitset>
#include <thread>

extern "C" {
#include <libswscale/swscale.h>
}

#include "cbs.h"
#include "config.h"
#include "input.h"
#include "main.h"
#include "platform/common.h"
#include "round_robin.h"
#include "sync.h"
#include "video.h"

#ifdef _WIN32
extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}
#endif

using namespace std::literals;
namespace video {

constexpr auto hevc_nalu = "\000\000\000\001("sv;
constexpr auto h264_nalu = "\000\000\000\001e"sv;

void free_ctx(AVCodecContext *ctx) {
  avcodec_free_context(&ctx);
}

void free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}

void free_buffer(AVBufferRef *ref) {
  av_buffer_unref(&ref);
}

using ctx_t       = util::safe_ptr<AVCodecContext, free_ctx>;
using frame_t     = util::safe_ptr<AVFrame, free_frame>;
using buffer_t    = util::safe_ptr<AVBufferRef, free_buffer>;
using sws_t       = util::safe_ptr<SwsContext, sws_freeContext>;
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
} // namespace nv


platf::mem_type_e map_dev_type(AVHWDeviceType type);
platf::pix_fmt_e map_pix_fmt(AVPixelFormat fmt);

util::Either<buffer_t, int> dxgi_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx);
util::Either<buffer_t, int> vaapi_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx);
util::Either<buffer_t, int> cuda_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx);

int hwframe_ctx(ctx_t &ctx, buffer_t &hwdevice, AVPixelFormat format);

class swdevice_t : public platf::hwdevice_t {
public:
  int convert(platf::img_t &img) override {
    av_frame_make_writable(sw_frame.get());

    const int linesizes[2] {
      img.row_pitch, 0
    };

    std::uint8_t *data[4];

    data[0] = sw_frame->data[0] + offsetY;
    if(sw_frame->format == AV_PIX_FMT_NV12) {
      data[1] = sw_frame->data[1] + offsetUV * 2;
      data[2] = nullptr;
    }
    else {
      data[1] = sw_frame->data[1] + offsetUV;
      data[2] = sw_frame->data[2] + offsetUV;
      data[3] = nullptr;
    }

    int ret = sws_scale(sws.get(), (std::uint8_t *const *)&img.data, linesizes, 0, img.height, data, sw_frame->linesize);
    if(ret <= 0) {
      BOOST_LOG(error) << "Couldn't convert image to required format and/or size"sv;

      return -1;
    }

    // If frame is not a software frame, it means we still need to transfer from main memory
    // to vram memory
    if(frame->hw_frames_ctx) {
      auto status = av_hwframe_transfer_data(frame, sw_frame.get(), 0);
      if(status < 0) {
        char string[AV_ERROR_MAX_STRING_SIZE];
        BOOST_LOG(error) << "Failed to transfer image data to hardware frame: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
        return -1;
      }
    }

    return 0;
  }

  int set_frame(AVFrame *frame) {
    this->frame = frame;

    // If it's a hwframe, allocate buffers for hardware
    if(frame->hw_frames_ctx) {
      hw_frame.reset(frame);

      if(av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0)) return -1;
    }

    if(!frame->hw_frames_ctx) {
      sw_frame.reset(frame);
    }

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    sws_setColorspaceDetails(sws.get(),
      sws_getCoefficients(SWS_CS_DEFAULT), 0,
      sws_getCoefficients(colorspace), color_range - 1,
      0, 1 << 16, 1 << 16);
  }

  /**
   * When preserving aspect ratio, ensure that padding is black
   */
  int prefill() {
    auto frame  = sw_frame ? sw_frame.get() : this->frame;
    auto width  = frame->width;
    auto height = frame->height;

    av_frame_get_buffer(frame, 0);
    sws_t sws {
      sws_getContext(
        width, height, AV_PIX_FMT_BGR0,
        width, height, (AVPixelFormat)frame->format,
        SWS_LANCZOS | SWS_ACCURATE_RND,
        nullptr, nullptr, nullptr)
    };

    if(!sws) {
      return -1;
    }

    util::buffer_t<std::uint32_t> img { (std::size_t)(width * height) };
    std::fill(std::begin(img), std::end(img), 0);

    const int linesizes[2] {
      width, 0
    };

    av_frame_make_writable(frame);

    auto data = img.begin();
    int ret   = sws_scale(sws.get(), (std::uint8_t *const *)&data, linesizes, 0, height, frame->data, frame->linesize);
    if(ret <= 0) {
      BOOST_LOG(error) << "Couldn't convert image to required format and/or size"sv;

      return -1;
    }

    return 0;
  }

  int init(int in_width, int in_height, AVFrame *frame, AVPixelFormat format) {
    // If the device used is hardware, yet the image resides on main memory
    if(frame->hw_frames_ctx) {
      sw_frame.reset(av_frame_alloc());

      sw_frame->width  = frame->width;
      sw_frame->height = frame->height;
      sw_frame->format = format;
    }
    else {
      this->frame = frame;
    }

    if(prefill()) {
      return -1;
    }

    auto out_width  = frame->width;
    auto out_height = frame->height;

    // Ensure aspect ratio is maintained
    auto scalar = std::fminf((float)out_width / in_width, (float)out_height / in_height);
    out_width   = in_width * scalar;
    out_height  = in_height * scalar;

    // result is always positive
    auto offsetW = (frame->width - out_width) / 2;
    auto offsetH = (frame->height - out_height) / 2;
    offsetUV     = (offsetW + offsetH * frame->width / 2) / 2;
    offsetY      = offsetW + offsetH * frame->width;

    sws.reset(sws_getContext(
      in_width, in_height, AV_PIX_FMT_BGR0,
      out_width, out_height, format,
      SWS_LANCZOS | SWS_ACCURATE_RND,
      nullptr, nullptr, nullptr));

    return sws ? 0 : -1;
  }

  ~swdevice_t() override {}

  // Store ownsership when frame is hw_frame
  frame_t hw_frame;

  frame_t sw_frame;
  sws_t sws;

  // offset of input image to output frame in pixels
  int offsetUV;
  int offsetY;
};

enum flag_e {
  DEFAULT           = 0x00,
  PARALLEL_ENCODING = 0x01,
  H264_ONLY         = 0x02, // When HEVC is to heavy
  LIMITED_GOP_SIZE  = 0x04, // Some encoders don't like it when you have an infinite GOP_SIZE. *cough* VAAPI *cough*
  SINGLE_SLICE_ONLY = 0x08, // Never use multiple slices <-- Older intel iGPU's ruin it for everyone else :P
};

struct encoder_t {
  std::string_view name;
  enum flag_e {
    PASSED,                // Is supported
    REF_FRAMES_RESTRICT,   // Set maximum reference frames
    REF_FRAMES_AUTOSELECT, // Allow encoder to select maximum reference frames (If !REF_FRAMES_RESTRICT --> REF_FRAMES_AUTOSELECT)
    SLICE,                 // Allow frame to be partitioned into multiple slices
    CBR,                   // Some encoders don't support CBR, if not supported --> attempt constant quantatication parameter instead
    DYNAMIC_RANGE,         // hdr
    VUI_PARAMETERS,        // AMD encoder with VAAPI doesn't add VUI parameters to SPS
    NALU_PREFIX_5b,        // libx264/libx265 have a 3-byte nalu prefix instead of 4-byte nalu prefix
    MAX_FLAGS
  };

  static std::string_view from_flag(flag_e flag) {
#define _CONVERT(x) \
  case flag_e::x:   \
    return #x##sv
    switch(flag) {
      _CONVERT(PASSED);
      _CONVERT(REF_FRAMES_RESTRICT);
      _CONVERT(REF_FRAMES_AUTOSELECT);
      _CONVERT(SLICE);
      _CONVERT(CBR);
      _CONVERT(DYNAMIC_RANGE);
      _CONVERT(VUI_PARAMETERS);
      _CONVERT(NALU_PREFIX_5b);
      _CONVERT(MAX_FLAGS);
    }
#undef _CONVERT

    return "unknown"sv;
  }

  struct option_t {
    KITTY_DEFAULT_CONSTR_MOVE(option_t)
    option_t(const option_t &) = default;

    std::string name;
    std::variant<int, int *, std::optional<int> *, std::string, std::string *> value;

    option_t(std::string &&name, decltype(value) &&value) : name { std::move(name) }, value { std::move(value) } {}
  };

  struct {
    int h264_high;
    int hevc_main;
    int hevc_main_10;
  } profile;

  AVHWDeviceType dev_type;
  AVPixelFormat dev_pix_fmt;

  AVPixelFormat static_pix_fmt;
  AVPixelFormat dynamic_pix_fmt;

  struct {
    std::vector<option_t> options;
    std::optional<option_t> qp;

    std::string name;
    std::bitset<MAX_FLAGS> capabilities;

    bool operator[](flag_e flag) const {
      return capabilities[(std::size_t)flag];
    }

    std::bitset<MAX_FLAGS>::reference operator[](flag_e flag) {
      return capabilities[(std::size_t)flag];
    }
  } hevc, h264;

  int flags;

  std::function<util::Either<buffer_t, int>(platf::hwdevice_t *hwdevice)> make_hwdevice_ctx;
};

class session_t {
public:
  session_t() = default;
  session_t(ctx_t &&ctx, std::shared_ptr<platf::hwdevice_t> &&device, int inject) : ctx { std::move(ctx) }, device { std::move(device) }, inject { inject } {}

  session_t(session_t &&other) noexcept = default;

  // Ensure objects are destroyed in the correct order
  session_t &operator=(session_t &&other) {
    device       = std::move(other.device);
    ctx          = std::move(other.ctx);
    replacements = std::move(other.replacements);
    sps          = std::move(other.sps);
    vps          = std::move(other.vps);

    inject = other.inject;

    return *this;
  }

  ctx_t ctx;
  std::shared_ptr<platf::hwdevice_t> device;

  std::vector<packet_raw_t::replace_t> replacements;

  cbs::nal_t sps;
  cbs::nal_t vps;

  // inject sps/vps data into idr pictures
  int inject;
};

struct sync_session_ctx_t {
  safe::signal_t *join_event;
  safe::mail_raw_t::event_t<bool> shutdown_event;
  safe::mail_raw_t::queue_t<packet_t> packets;
  safe::mail_raw_t::event_t<bool> idr_events;
  safe::mail_raw_t::event_t<input::touch_port_t> touch_port_events;

  config_t config;
  int frame_nr;
  void *channel_data;
};

struct sync_session_t {
  sync_session_ctx_t *ctx;

  platf::img_t *img_tmp;
  session_t session;
};

using encode_session_ctx_queue_t = safe::queue_t<sync_session_ctx_t>;
using encode_e                   = platf::capture_e;

struct capture_ctx_t {
  img_event_t images;
  int framerate;
};

struct capture_thread_async_ctx_t {
  std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue;
  std::thread capture_thread;

  safe::signal_t reinit_event;
  const encoder_t *encoder_p;
  util::sync_t<std::weak_ptr<platf::display_t>> display_wp;
};

struct capture_thread_sync_ctx_t {
  encode_session_ctx_queue_t encode_session_ctx_queue { 30 };
};

int start_capture_sync(capture_thread_sync_ctx_t &ctx);
void end_capture_sync(capture_thread_sync_ctx_t &ctx);
int start_capture_async(capture_thread_async_ctx_t &ctx);
void end_capture_async(capture_thread_async_ctx_t &ctx);

// Keep a reference counter to ensure the capture thread only runs when other threads have a reference to the capture thread
auto capture_thread_async = safe::make_shared<capture_thread_async_ctx_t>(start_capture_async, end_capture_async);
auto capture_thread_sync  = safe::make_shared<capture_thread_sync_ctx_t>(start_capture_sync, end_capture_sync);

static encoder_t nvenc {
  "nvenc"sv,
  { (int)nv::profile_h264_e::high, (int)nv::profile_hevc_e::main, (int)nv::profile_hevc_e::main_10 },
#ifdef _WIN32
  AV_HWDEVICE_TYPE_D3D11VA,
  AV_PIX_FMT_D3D11,
#else
  AV_HWDEVICE_TYPE_CUDA,
  AV_PIX_FMT_CUDA,
#endif
  AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
  {
    {
      { "forced-idr"s, 1 },
      { "zerolatency"s, 1 },
      { "preset"s, &config::video.nv.preset },
      { "rc"s, &config::video.nv.rc },
    },
    std::nullopt,
    "hevc_nvenc"s,
  },
  {
    {
      { "forced-idr"s, 1 },
      { "zerolatency"s, 1 },
      { "preset"s, &config::video.nv.preset },
      { "rc"s, &config::video.nv.rc },
      { "coder"s, &config::video.nv.coder },
    },
    std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
    "h264_nvenc"s,
  },
#ifdef _WIN32
  DEFAULT,
  dxgi_make_hwdevice_ctx
#else
  PARALLEL_ENCODING,
  cuda_make_hwdevice_ctx
#endif
};

#ifdef _WIN32
static encoder_t amdvce {
  "amdvce"sv,
  { FF_PROFILE_H264_HIGH, FF_PROFILE_HEVC_MAIN },
  AV_HWDEVICE_TYPE_D3D11VA,
  AV_PIX_FMT_D3D11,
  AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
  {
    {
      { "header_insertion_mode"s, "idr"s },
      { "gops_per_idr"s, 30 },
      { "usage"s, "ultralowlatency"s },
      { "quality"s, &config::video.amd.quality },
      { "rc"s, &config::video.amd.rc_hevc },
    },
    std::make_optional<encoder_t::option_t>({ "qp_p"s, &config::video.qp }),
    "hevc_amf"s,
  },
  {
    {
      { "usage"s, "ultralowlatency"s },
      { "quality"s, &config::video.amd.quality },
      { "rc"s, &config::video.amd.rc_h264 },
      { "log_to_dbg"s, "1"s },
    },
    std::make_optional<encoder_t::option_t>({ "qp_p"s, &config::video.qp }),
    "h264_amf"s,
  },
  DEFAULT,
  dxgi_make_hwdevice_ctx
};
#endif

static encoder_t software {
  "software"sv,
  { FF_PROFILE_H264_HIGH, FF_PROFILE_HEVC_MAIN, FF_PROFILE_HEVC_MAIN_10 },
  AV_HWDEVICE_TYPE_NONE,
  AV_PIX_FMT_NONE,
  AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10,
  {
    // x265's Info SEI is so long that it causes the IDR picture data to be
    // kicked to the 2nd packet in the frame, breaking Moonlight's parsing logic.
    // It also looks like gop_size isn't passed on to x265, so we have to set
    // 'keyint=-1' in the parameters ourselves.
    {
      { "forced-idr"s, 1 },
      { "x265-params"s, "info=0:keyint=-1"s },
      { "preset"s, &config::video.sw.preset },
      { "tune"s, &config::video.sw.tune },
    },
    std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
    "libx265"s,
  },
  {
    {
      { "preset"s, &config::video.sw.preset },
      { "tune"s, &config::video.sw.tune },
    },
    std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
    "libx264"s,
  },
  H264_ONLY | PARALLEL_ENCODING,

  nullptr
};

#ifdef __linux__
static encoder_t vaapi {
  "vaapi"sv,
  { FF_PROFILE_H264_HIGH, FF_PROFILE_HEVC_MAIN, FF_PROFILE_HEVC_MAIN_10 },
  AV_HWDEVICE_TYPE_VAAPI,
  AV_PIX_FMT_VAAPI,
  AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P10,
  {
    {
      { "sei"s, 0 },
      { "idr_interval"s, std::numeric_limits<int>::max() },
    },
    std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
    "hevc_vaapi"s,
  },
  {
    {
      { "sei"s, 0 },
      { "idr_interval"s, std::numeric_limits<int>::max() },
    },
    std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
    "h264_vaapi"s,
  },
  LIMITED_GOP_SIZE | PARALLEL_ENCODING | SINGLE_SLICE_ONLY,

  vaapi_make_hwdevice_ctx
};
#endif

static std::vector<encoder_t> encoders {
  nvenc,
#ifdef _WIN32
  amdvce,
#endif
#ifdef __linux__
  vaapi,
#endif
  software
};

void reset_display(std::shared_ptr<platf::display_t> &disp, AVHWDeviceType type, const std::string &display_name, int framerate) {
  // We try this twice, in case we still get an error on reinitialization
  for(int x = 0; x < 2; ++x) {
    disp.reset();
    disp = platf::display(map_dev_type(type), display_name, framerate);
    if(disp) {
      break;
    }

    std::this_thread::sleep_for(200ms);
  }
}

void captureThread(
  std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue,
  util::sync_t<std::weak_ptr<platf::display_t>> &display_wp,
  safe::signal_t &reinit_event,
  const encoder_t &encoder) {
  std::vector<capture_ctx_t> capture_ctxs;

  auto fg = util::fail_guard([&]() {
    capture_ctx_queue->stop();

    // Stop all sessions listening to this thread
    for(auto &capture_ctx : capture_ctxs) {
      capture_ctx.images->stop();
    }
    for(auto &capture_ctx : capture_ctx_queue->unsafe()) {
      capture_ctx.images->stop();
    }
  });

  auto switch_display_event = mail::man->event<int>(mail::switch_display);

  // Get all the monitor names now, rather than at boot, to
  // get the most up-to-date list available monitors
  auto display_names = platf::display_names(map_dev_type(encoder.dev_type));
  int display_p      = 0;

  if(display_names.empty()) {
    display_names.emplace_back(config::video.output_name);
  }

  for(int x = 0; x < display_names.size(); ++x) {
    if(display_names[x] == config::video.output_name) {
      display_p = x;

      break;
    }
  }

  if(auto capture_ctx = capture_ctx_queue->pop()) {
    capture_ctxs.emplace_back(std::move(*capture_ctx));
  }

  auto disp = platf::display(map_dev_type(encoder.dev_type), display_names[display_p], capture_ctxs.front().framerate);
  if(!disp) {
    return;
  }
  display_wp = disp;

  std::vector<std::shared_ptr<platf::img_t>> imgs(12);
  auto round_robin = util::make_round_robin<std::shared_ptr<platf::img_t>>(std::begin(imgs), std::end(imgs));

  for(auto &img : imgs) {
    img = disp->alloc_img();
    if(!img) {
      BOOST_LOG(error) << "Couldn't initialize an image"sv;
      return;
    }
  }

  while(capture_ctx_queue->running()) {
    bool artificial_reinit = false;

    auto status = disp->capture([&](std::shared_ptr<platf::img_t> &img) -> std::shared_ptr<platf::img_t> {
      KITTY_WHILE_LOOP(auto capture_ctx = std::begin(capture_ctxs), capture_ctx != std::end(capture_ctxs), {
        if(!capture_ctx->images->running()) {
          capture_ctx = capture_ctxs.erase(capture_ctx);

          continue;
        }

        capture_ctx->images->raise(img);
        ++capture_ctx;
      })

      if(!capture_ctx_queue->running()) {
        return nullptr;
      }
      while(capture_ctx_queue->peek()) {
        capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));
      }

      if(switch_display_event->peek()) {
        artificial_reinit = true;

        display_p = std::clamp(*switch_display_event->pop(), 0, (int)display_names.size() - 1);
        return nullptr;
      }

      auto &next_img = *round_robin++;
      while(next_img.use_count() > 1) {}

      return next_img;
    },
      *round_robin++, &display_cursor);


    if(artificial_reinit && status != platf::capture_e::error) {
      status = platf::capture_e::reinit;

      artificial_reinit = false;
    }

    switch(status) {
    case platf::capture_e::reinit: {
      reinit_event.raise(true);

      // Some classes of images contain references to the display --> display won't delete unless img is deleted
      for(auto &img : imgs) {
        img.reset();
      }

      // display_wp is modified in this thread only
      // Wait for the other shared_ptr's of display to be destroyed.
      // New displays will only be created in this thread.
      while(display_wp->use_count() != 1) {
        std::this_thread::sleep_for(100ms);
      }

      while(capture_ctx_queue->running()) {
        reset_display(disp, encoder.dev_type, display_names[display_p], capture_ctxs.front().framerate);

        if(disp) {
          break;
        }
        std::this_thread::sleep_for(200ms);
      }
      if(!disp) {
        return;
      }

      display_wp = disp;

      // Re-allocate images
      for(auto &img : imgs) {
        img = disp->alloc_img();
        if(!img) {
          BOOST_LOG(error) << "Couldn't initialize an image"sv;
          return;
        }
      }

      reinit_event.reset();
      continue;
    }
    case platf::capture_e::error:
    case platf::capture_e::ok:
    case platf::capture_e::timeout:
      return;
    default:
      BOOST_LOG(error) << "Unrecognized capture status ["sv << (int)status << ']';
      return;
    }
  }
}

int encode(int64_t frame_nr, session_t &session, frame_t::pointer frame, safe::mail_raw_t::queue_t<packet_t> &packets, void *channel_data) {
  frame->pts = frame_nr;

  auto &ctx = session.ctx;

  auto &sps = session.sps;
  auto &vps = session.vps;

  /* send the frame to the encoder */
  auto ret = avcodec_send_frame(ctx.get(), frame);
  if(ret < 0) {
    char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
    BOOST_LOG(error) << "Could not send a frame for encoding: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, ret);

    return -1;
  }

  while(ret >= 0) {
    auto packet = std::make_unique<packet_t::element_type>(nullptr);

    ret = avcodec_receive_packet(ctx.get(), packet.get());
    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return 0;
    }
    else if(ret < 0) {
      return ret;
    }

    if(session.inject) {
      if(session.inject == 1) {
        auto h264 = cbs::make_sps_h264(ctx.get(), packet.get());

        sps = std::move(h264.sps);
      }
      else {
        auto hevc = cbs::make_sps_hevc(ctx.get(), packet.get());

        sps = std::move(hevc.sps);
        vps = std::move(hevc.vps);

        session.replacements.emplace_back(
          std::string_view((char *)std::begin(vps.old), vps.old.size()),
          std::string_view((char *)std::begin(vps._new), vps._new.size()));
      }

      session.inject = 0;


      session.replacements.emplace_back(
        std::string_view((char *)std::begin(sps.old), sps.old.size()),
        std::string_view((char *)std::begin(sps._new), sps._new.size()));
    }

    packet->replacements = &session.replacements;
    packet->channel_data = channel_data;
    packets->raise(std::move(packet));
  }

  return 0;
}

std::optional<session_t> make_session(const encoder_t &encoder, const config_t &config, int width, int height, std::shared_ptr<platf::hwdevice_t> &&hwdevice) {
  bool hardware = encoder.dev_type != AV_HWDEVICE_TYPE_NONE;

  auto &video_format = config.videoFormat == 0 ? encoder.h264 : encoder.hevc;
  if(!video_format[encoder_t::PASSED]) {
    BOOST_LOG(error) << encoder.name << ": "sv << video_format.name << " mode not supported"sv;
    return std::nullopt;
  }

  if(config.dynamicRange && !video_format[encoder_t::DYNAMIC_RANGE]) {
    BOOST_LOG(error) << video_format.name << ": dynamic range not supported"sv;
    return std::nullopt;
  }

  auto codec = avcodec_find_encoder_by_name(video_format.name.c_str());
  if(!codec) {
    BOOST_LOG(error) << "Couldn't open ["sv << video_format.name << ']';

    return std::nullopt;
  }

  ctx_t ctx { avcodec_alloc_context3(codec) };
  ctx->width     = config.width;
  ctx->height    = config.height;
  ctx->time_base = AVRational { 1, config.framerate };
  ctx->framerate = AVRational { config.framerate, 1 };

  if(config.videoFormat == 0) {
    ctx->profile = encoder.profile.h264_high;
  }
  else if(config.dynamicRange == 0) {
    ctx->profile = encoder.profile.hevc_main;
  }
  else {
    ctx->profile = encoder.profile.hevc_main_10;
  }

  // B-frames delay decoder output, so never use them
  ctx->max_b_frames = 0;

  // Use an infinite GOP length since I-frames are generated on demand
  ctx->gop_size = encoder.flags & LIMITED_GOP_SIZE ?
                    std::numeric_limits<std::int16_t>::max() :
                    std::numeric_limits<int>::max();

  ctx->keyint_min = std::numeric_limits<int>::max();

  if(config.numRefFrames == 0) {
    ctx->refs = video_format[encoder_t::REF_FRAMES_AUTOSELECT] ? 0 : 16;
  }
  else {
    // Some client decoders have limits on the number of reference frames
    ctx->refs = video_format[encoder_t::REF_FRAMES_RESTRICT] ? config.numRefFrames : 0;
  }

  ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
  ctx->flags2 |= AV_CODEC_FLAG2_FAST;

  ctx->color_range = (config.encoderCscMode & 0x1) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

  int sws_color_space;
  switch(config.encoderCscMode >> 1) {
  case 0:
  default:
    // Rec. 601
    BOOST_LOG(info) << "Color coding [Rec. 601]"sv;
    ctx->color_primaries = AVCOL_PRI_SMPTE170M;
    ctx->color_trc       = AVCOL_TRC_SMPTE170M;
    ctx->colorspace      = AVCOL_SPC_SMPTE170M;
    sws_color_space      = SWS_CS_SMPTE170M;
    break;

  case 1:
    // Rec. 709
    BOOST_LOG(info) << "Color coding [Rec. 709]"sv;
    ctx->color_primaries = AVCOL_PRI_BT709;
    ctx->color_trc       = AVCOL_TRC_BT709;
    ctx->colorspace      = AVCOL_SPC_BT709;
    sws_color_space      = SWS_CS_ITU709;
    break;

  case 2:
    // Rec. 2020
    BOOST_LOG(info) << "Color coding [Rec. 2020]"sv;
    ctx->color_primaries = AVCOL_PRI_BT2020;
    ctx->color_trc       = AVCOL_TRC_BT2020_10;
    ctx->colorspace      = AVCOL_SPC_BT2020_NCL;
    sws_color_space      = SWS_CS_BT2020;
    break;
  }
  BOOST_LOG(info) << "Color range: ["sv << ((config.encoderCscMode & 0x1) ? "JPEG"sv : "MPEG"sv) << ']';

  AVPixelFormat sw_fmt;
  if(config.dynamicRange == 0) {
    sw_fmt = encoder.static_pix_fmt;
  }
  else {
    sw_fmt = encoder.dynamic_pix_fmt;
  }

  // Used by cbs::make_sps_hevc
  ctx->sw_pix_fmt = sw_fmt;

  buffer_t hwdevice_ctx;
  if(hardware) {
    ctx->pix_fmt = encoder.dev_pix_fmt;

    auto buf_or_error = encoder.make_hwdevice_ctx(hwdevice.get());
    if(buf_or_error.has_right()) {
      return std::nullopt;
    }

    hwdevice_ctx = std::move(buf_or_error.left());
    if(hwframe_ctx(ctx, hwdevice_ctx, sw_fmt)) {
      return std::nullopt;
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

  if(!video_format[encoder_t::SLICE]) {
    ctx->slices = 1;
  }

  ctx->thread_type  = FF_THREAD_SLICE;
  ctx->thread_count = ctx->slices;

  AVDictionary *options { nullptr };
  auto handle_option = [&options](const encoder_t::option_t &option) {
    std::visit(
      util::overloaded {
        [&](int v) { av_dict_set_int(&options, option.name.c_str(), v, 0); },
        [&](int *v) { av_dict_set_int(&options, option.name.c_str(), *v, 0); },
        [&](std::optional<int> *v) { if(*v) av_dict_set_int(&options, option.name.c_str(), **v, 0); },
        [&](const std::string &v) { av_dict_set(&options, option.name.c_str(), v.c_str(), 0); },
        [&](std::string *v) { if(!v->empty()) av_dict_set(&options, option.name.c_str(), v->c_str(), 0); } },
      option.value);
  };

  for(auto &option : video_format.options) {
    handle_option(option);
  }

  if(video_format[encoder_t::CBR]) {
    auto bitrate        = config.bitrate * 1000;
    ctx->rc_max_rate    = bitrate;
    ctx->rc_buffer_size = bitrate / config.framerate;
    ctx->bit_rate       = bitrate;
    ctx->rc_min_rate    = bitrate;
  }
  else if(video_format.qp) {
    handle_option(*video_format.qp);
  }
  else {
    BOOST_LOG(error) << "Couldn't set video quality: encoder "sv << encoder.name << " doesn't support qp"sv;
    return std::nullopt;
  }

  if(auto status = avcodec_open2(ctx.get(), codec, &options)) {
    char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
    BOOST_LOG(error)
      << "Could not open codec ["sv
      << video_format.name << "]: "sv
      << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, status);

    return std::nullopt;
  }

  frame_t frame { av_frame_alloc() };
  frame->format = ctx->pix_fmt;
  frame->width  = ctx->width;
  frame->height = ctx->height;


  if(hardware) {
    frame->hw_frames_ctx = av_buffer_ref(ctx->hw_frames_ctx);
  }

  std::shared_ptr<platf::hwdevice_t> device;

  if(!hwdevice->data) {
    auto device_tmp = std::make_unique<swdevice_t>();

    if(device_tmp->init(width, height, frame.get(), sw_fmt)) {
      return std::nullopt;
    }

    device = std::move(device_tmp);
  }
  else {
    device = std::move(hwdevice);
  }

  if(device->set_frame(frame.release())) {
    return std::nullopt;
  }

  device->set_colorspace(sws_color_space, ctx->color_range);

  session_t session {
    std::move(ctx),
    std::move(device),

    // 0 ==> don't inject, 1 ==> inject for h264, 2 ==> inject for hevc
    (1 - (int)video_format[encoder_t::VUI_PARAMETERS]) * (1 + config.videoFormat),
  };

  if(!video_format[encoder_t::NALU_PREFIX_5b]) {
    auto nalu_prefix = config.videoFormat ? hevc_nalu : h264_nalu;

    session.replacements.emplace_back(nalu_prefix.substr(1), nalu_prefix);
  }

  return std::make_optional(std::move(session));
}

void encode_run(
  int &frame_nr, // Store progress of the frame number
  safe::mail_t mail,
  img_event_t images,
  config_t config,
  int width, int height,
  std::shared_ptr<platf::hwdevice_t> &&hwdevice,
  safe::signal_t &reinit_event,
  const encoder_t &encoder,
  void *channel_data) {

  auto session = make_session(encoder, config, width, height, std::move(hwdevice));
  if(!session) {
    return;
  }

  auto frame = session->device->frame;

  auto shutdown_event = mail->event<bool>(mail::shutdown);
  auto packets        = mail::man->queue<packet_t>(mail::video_packets);
  auto idr_events     = mail->event<bool>(mail::idr);

  while(true) {
    if(shutdown_event->peek() || reinit_event.peek() || !images->running()) {
      break;
    }

    if(idr_events->peek()) {
      frame->pict_type = AV_PICTURE_TYPE_I;
      frame->key_frame = 1;

      idr_events->pop();
    }

    if(!frame->key_frame || images->peek()) {
      if(auto img = images->pop(100ms)) {
        session->device->convert(*img);
      }
      else if(images->running()) {
        continue;
      }
      else {
        break;
      }
    }

    if(encode(frame_nr++, *session, frame, packets, channel_data)) {
      BOOST_LOG(error) << "Could not encode video packet"sv;
      return;
    }

    frame->pict_type = AV_PICTURE_TYPE_NONE;
    frame->key_frame = 0;
  }
}

input::touch_port_t make_port(platf::display_t *display, const config_t &config) {
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
    display->offset_x,
    display->offset_y,
    config.width,
    config.height,
    display->env_width,
    display->env_height,
    offsetX,
    offsetY,
    1.0f / scalar,
  };
}

std::optional<sync_session_t> make_synced_session(platf::display_t *disp, const encoder_t &encoder, platf::img_t &img, sync_session_ctx_t &ctx) {
  sync_session_t encode_session;

  encode_session.ctx = &ctx;

  auto pix_fmt  = ctx.config.dynamicRange == 0 ? map_pix_fmt(encoder.static_pix_fmt) : map_pix_fmt(encoder.dynamic_pix_fmt);
  auto hwdevice = disp->make_hwdevice(pix_fmt);
  if(!hwdevice) {
    return std::nullopt;
  }

  // absolute mouse coordinates require that the dimensions of the screen are known
  ctx.touch_port_events->raise(make_port(disp, ctx.config));

  auto session = make_session(encoder, ctx.config, img.width, img.height, std::move(hwdevice));
  if(!session) {
    return std::nullopt;
  }

  encode_session.session = std::move(*session);

  return std::move(encode_session);
}

encode_e encode_run_sync(
  std::vector<std::unique_ptr<sync_session_ctx_t>> &synced_session_ctxs,
  encode_session_ctx_queue_t &encode_session_ctx_queue) {

  const auto &encoder = encoders.front();
  auto display_names  = platf::display_names(map_dev_type(encoder.dev_type));
  int display_p       = 0;

  if(display_names.empty()) {
    display_names.emplace_back(config::video.output_name);
  }

  for(int x = 0; x < display_names.size(); ++x) {
    if(display_names[x] == config::video.output_name) {
      display_p = x;

      break;
    }
  }

  std::shared_ptr<platf::display_t> disp;

  auto switch_display_event = mail::man->event<int>(mail::switch_display);

  if(synced_session_ctxs.empty()) {
    auto ctx = encode_session_ctx_queue.pop();
    if(!ctx) {
      return encode_e::ok;
    }

    synced_session_ctxs.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*ctx)));
  }

  int framerate = synced_session_ctxs.front()->config.framerate;

  while(encode_session_ctx_queue.running()) {
    reset_display(disp, encoder.dev_type, display_names[display_p], framerate);
    if(disp) {
      break;
    }

    std::this_thread::sleep_for(200ms);
  }

  if(!disp) {
    return encode_e::error;
  }

  auto img = disp->alloc_img();
  if(!img || disp->dummy_img(img.get())) {
    return encode_e::error;
  }

  std::vector<sync_session_t> synced_sessions;
  for(auto &ctx : synced_session_ctxs) {
    auto synced_session = make_synced_session(disp.get(), encoder, *img, *ctx);
    if(!synced_session) {
      return encode_e::error;
    }

    synced_sessions.emplace_back(std::move(*synced_session));
  }

  auto ec = platf::capture_e::ok;
  while(encode_session_ctx_queue.running()) {
    auto snapshot_cb = [&](std::shared_ptr<platf::img_t> &img) -> std::shared_ptr<platf::img_t> {
      while(encode_session_ctx_queue.peek()) {
        auto encode_session_ctx = encode_session_ctx_queue.pop();
        if(!encode_session_ctx) {
          return nullptr;
        }

        synced_session_ctxs.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*encode_session_ctx)));

        auto encode_session = make_synced_session(disp.get(), encoder, *img, *synced_session_ctxs.back());
        if(!encode_session) {
          ec = platf::capture_e::error;
          return nullptr;
        }

        synced_sessions.emplace_back(std::move(*encode_session));
      }

      KITTY_WHILE_LOOP(auto pos = std::begin(synced_sessions), pos != std::end(synced_sessions), {
        auto frame = pos->session.device->frame;
        auto ctx   = pos->ctx;
        if(ctx->shutdown_event->peek()) {
          // Let waiting thread know it can delete shutdown_event
          ctx->join_event->raise(true);

          pos = synced_sessions.erase(pos);
          synced_session_ctxs.erase(std::find_if(std::begin(synced_session_ctxs), std::end(synced_session_ctxs), [&ctx_p = ctx](auto &ctx) {
            return ctx.get() == ctx_p;
          }));

          if(synced_sessions.empty()) {
            return nullptr;
          }

          continue;
        }

        if(ctx->idr_events->peek()) {
          frame->pict_type = AV_PICTURE_TYPE_I;
          frame->key_frame = 1;

          ctx->idr_events->pop();
        }

        if(pos->session.device->convert(*img)) {
          BOOST_LOG(error) << "Could not convert image"sv;
          ctx->shutdown_event->raise(true);

          continue;
        }

        if(encode(ctx->frame_nr++, pos->session, frame, ctx->packets, ctx->channel_data)) {
          BOOST_LOG(error) << "Could not encode video packet"sv;
          ctx->shutdown_event->raise(true);

          continue;
        }

        frame->pict_type = AV_PICTURE_TYPE_NONE;
        frame->key_frame = 0;

        ++pos;
      })

      if(switch_display_event->peek()) {
        ec = platf::capture_e::reinit;

        display_p = std::clamp(*switch_display_event->pop(), 0, (int)display_names.size() - 1);
        return nullptr;
      }

      return img;
    };

    auto status = disp->capture(std::move(snapshot_cb), img, &display_cursor);
    switch(status) {
    case platf::capture_e::reinit:
    case platf::capture_e::error:
    case platf::capture_e::ok:
    case platf::capture_e::timeout:
      return ec != platf::capture_e::ok ? ec : status;
    }
  }

  return encode_e::ok;
}

void captureThreadSync() {
  auto ref = capture_thread_sync.ref();

  std::vector<std::unique_ptr<sync_session_ctx_t>> synced_session_ctxs;

  auto &ctx = ref->encode_session_ctx_queue;
  auto lg   = util::fail_guard([&]() {
    ctx.stop();

    for(auto &ctx : synced_session_ctxs) {
      ctx->shutdown_event->raise(true);
      ctx->join_event->raise(true);
    }

    for(auto &ctx : ctx.unsafe()) {
      ctx.shutdown_event->raise(true);
      ctx.join_event->raise(true);
    }
  });

  while(encode_run_sync(synced_session_ctxs, ctx) == encode_e::reinit) {}
}

void capture_async(
  safe::mail_t mail,
  config_t &config,
  void *channel_data) {

  auto shutdown_event = mail->event<bool>(mail::shutdown);

  auto images = std::make_shared<img_event_t::element_type>();
  auto lg     = util::fail_guard([&]() {
    images->stop();
    shutdown_event->raise(true);
  });

  auto ref = capture_thread_async.ref();
  if(!ref) {
    return;
  }

  ref->capture_ctx_queue->raise(capture_ctx_t {
    images, config.framerate });

  if(!ref->capture_ctx_queue->running()) {
    return;
  }

  int frame_nr = 1;

  auto touch_port_event = mail->event<input::touch_port_t>(mail::touch_port);

  while(!shutdown_event->peek() && images->running()) {
    // Wait for the main capture event when the display is being reinitialized
    if(ref->reinit_event.peek()) {
      std::this_thread::sleep_for(100ms);
      continue;
    }
    // Wait for the display to be ready
    std::shared_ptr<platf::display_t> display;
    {
      auto lg = ref->display_wp.lock();
      if(ref->display_wp->expired()) {
        continue;
      }

      display = ref->display_wp->lock();
    }

    auto &encoder = encoders.front();
    auto pix_fmt  = config.dynamicRange == 0 ? map_pix_fmt(encoder.static_pix_fmt) : map_pix_fmt(encoder.dynamic_pix_fmt);
    auto hwdevice = display->make_hwdevice(pix_fmt);
    if(!hwdevice) {
      return;
    }

    auto dummy_img = display->alloc_img();
    if(!dummy_img || display->dummy_img(dummy_img.get())) {
      return;
    }

    images->raise(std::move(dummy_img));

    // absolute mouse coordinates require that the dimensions of the screen are known
    touch_port_event->raise(make_port(display.get(), config));

    encode_run(
      frame_nr,
      mail, images,
      config, display->width, display->height,
      std::move(hwdevice),
      ref->reinit_event, *ref->encoder_p,
      channel_data);
  }
}

void capture(
  safe::mail_t mail,
  config_t config,
  void *channel_data) {

  auto idr_events = mail->event<bool>(mail::idr);

  idr_events->raise(true);
  if(encoders.front().flags & PARALLEL_ENCODING) {
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
  VUI_PARAMS     = 0x01,
  NALU_PREFIX_5b = 0x02,
};

int validate_config(std::shared_ptr<platf::display_t> &disp, const encoder_t &encoder, const config_t &config) {
  reset_display(disp, encoder.dev_type, config::video.output_name, config.framerate);
  if(!disp) {
    return -1;
  }

  auto pix_fmt  = config.dynamicRange == 0 ? map_pix_fmt(encoder.static_pix_fmt) : map_pix_fmt(encoder.dynamic_pix_fmt);
  auto hwdevice = disp->make_hwdevice(pix_fmt);
  if(!hwdevice) {
    return -1;
  }

  auto session = make_session(encoder, config, disp->width, disp->height, std::move(hwdevice));
  if(!session) {
    return -1;
  }

  auto img = disp->alloc_img();
  if(!img || disp->dummy_img(img.get())) {
    return -1;
  }
  if(session->device->convert(*img)) {
    return -1;
  }

  auto frame = session->device->frame;

  frame->pict_type = AV_PICTURE_TYPE_I;

  auto packets = mail::man->queue<packet_t>(mail::video_packets);
  while(!packets->peek()) {
    if(encode(1, *session, frame, packets, nullptr)) {
      return -1;
    }
  }

  auto packet = packets->pop();
  if(!(packet->flags & AV_PKT_FLAG_KEY)) {
    BOOST_LOG(error) << "First packet type is not an IDR frame"sv;

    return -1;
  }

  int flag = 0;
  if(cbs::validate_sps(&*packet, config.videoFormat ? AV_CODEC_ID_H265 : AV_CODEC_ID_H264)) {
    flag |= VUI_PARAMS;
  }

  auto nalu_prefix = config.videoFormat ? hevc_nalu : h264_nalu;
  std::string_view payload { (char *)packet->data, (std::size_t)packet->size };
  if(std::search(std::begin(payload), std::end(payload), std::begin(nalu_prefix), std::end(nalu_prefix)) != std::end(payload)) {
    flag |= NALU_PREFIX_5b;
  }

  return flag;
}

bool validate_encoder(encoder_t &encoder) {
  std::shared_ptr<platf::display_t> disp;

  BOOST_LOG(info) << "Trying encoder ["sv << encoder.name << ']';
  auto fg = util::fail_guard([&]() {
    BOOST_LOG(info) << "Encoder ["sv << encoder.name << "] failed"sv;
  });

  auto force_hevc = config::video.hevc_mode >= 2;
  auto test_hevc  = force_hevc || (config::video.hevc_mode == 0 && !(encoder.flags & H264_ONLY));

  encoder.h264.capabilities.set();
  encoder.hevc.capabilities.set();

  encoder.hevc[encoder_t::PASSED] = test_hevc;

  // First, test encoder viability
  config_t config_max_ref_frames { 1920, 1080, 60, 1000, 1, 1, 1, 0, 0 };
  config_t config_autoselect { 1920, 1080, 60, 1000, 1, 0, 1, 0, 0 };

retry:
  auto max_ref_frames_h264 = validate_config(disp, encoder, config_max_ref_frames);
  auto autoselect_h264     = validate_config(disp, encoder, config_autoselect);

  if(max_ref_frames_h264 < 0 && autoselect_h264 < 0) {
    if(encoder.h264.qp && encoder.h264[encoder_t::CBR]) {
      // It's possible the encoder isn't accepting Constant Bit Rate. Turn off CBR and make another attempt
      encoder.h264.capabilities.set();
      encoder.h264[encoder_t::CBR] = false;
      goto retry;
    }
    return false;
  }

  std::vector<std::pair<validate_flag_e, encoder_t::flag_e>> packet_deficiencies {
    { VUI_PARAMS, encoder_t::VUI_PARAMETERS },
    { NALU_PREFIX_5b, encoder_t::NALU_PREFIX_5b },
  };

  for(auto [validate_flag, encoder_flag] : packet_deficiencies) {
    encoder.h264[encoder_flag] = (max_ref_frames_h264 & validate_flag && autoselect_h264 & validate_flag);
  }

  encoder.h264[encoder_t::REF_FRAMES_RESTRICT]   = max_ref_frames_h264 >= 0;
  encoder.h264[encoder_t::REF_FRAMES_AUTOSELECT] = autoselect_h264 >= 0;
  encoder.h264[encoder_t::PASSED]                = true;

  encoder.h264[encoder_t::SLICE] = validate_config(disp, encoder, config_max_ref_frames);
  if(test_hevc) {
    config_max_ref_frames.videoFormat = 1;
    config_autoselect.videoFormat     = 1;

  retry_hevc:
    auto max_ref_frames_hevc = validate_config(disp, encoder, config_max_ref_frames);
    auto autoselect_hevc     = validate_config(disp, encoder, config_autoselect);

    // If HEVC must be supported, but it is not supported
    if(max_ref_frames_hevc < 0 && autoselect_hevc < 0) {
      if(encoder.hevc.qp && encoder.hevc[encoder_t::CBR]) {
        // It's possible the encoder isn't accepting Constant Bit Rate. Turn off CBR and make another attempt
        encoder.hevc.capabilities.set();
        encoder.hevc[encoder_t::CBR] = false;
        goto retry_hevc;
      }

      if(force_hevc) {
        return false;
      }
    }

    for(auto [validate_flag, encoder_flag] : packet_deficiencies) {
      encoder.hevc[encoder_flag] = (max_ref_frames_hevc & validate_flag && autoselect_hevc & validate_flag);
    }

    encoder.hevc[encoder_t::REF_FRAMES_RESTRICT]   = max_ref_frames_hevc >= 0;
    encoder.hevc[encoder_t::REF_FRAMES_AUTOSELECT] = autoselect_hevc >= 0;

    encoder.hevc[encoder_t::PASSED] = max_ref_frames_hevc >= 0 || autoselect_hevc >= 0;
  }

  std::vector<std::pair<encoder_t::flag_e, config_t>> configs {
    { encoder_t::DYNAMIC_RANGE, { 1920, 1080, 60, 1000, 1, 0, 3, 1, 1 } },
  };

  if(!(encoder.flags & SINGLE_SLICE_ONLY)) {
    configs.emplace_back(
      std::pair<encoder_t::flag_e, config_t> { encoder_t::SLICE, { 1920, 1080, 60, 1000, 2, 1, 1, 0, 0 } });
  }

  for(auto &[flag, config] : configs) {
    auto h264 = config;
    auto hevc = config;

    h264.videoFormat = 0;
    hevc.videoFormat = 1;

    encoder.h264[flag] = validate_config(disp, encoder, h264) >= 0;
    if(encoder.hevc[encoder_t::PASSED]) {
      encoder.hevc[flag] = validate_config(disp, encoder, hevc) >= 0;
    }
  }

  if(encoder.flags & SINGLE_SLICE_ONLY) {
    encoder.h264.capabilities[encoder_t::SLICE] = false;
    encoder.hevc.capabilities[encoder_t::SLICE] = false;
  }

  encoder.h264[encoder_t::VUI_PARAMETERS] = encoder.h264[encoder_t::VUI_PARAMETERS] && !config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];
  encoder.hevc[encoder_t::VUI_PARAMETERS] = encoder.hevc[encoder_t::VUI_PARAMETERS] && !config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];

  if(!encoder.h264[encoder_t::VUI_PARAMETERS]) {
    BOOST_LOG(warning) << encoder.name << ": h264 missing sps->vui parameters"sv;
  }
  if(encoder.hevc[encoder_t::PASSED] && !encoder.hevc[encoder_t::VUI_PARAMETERS]) {
    BOOST_LOG(warning) << encoder.name << ": hevc missing sps->vui parameters"sv;
  }

  if(!encoder.h264[encoder_t::NALU_PREFIX_5b]) {
    BOOST_LOG(warning) << encoder.name << ": h264: replacing nalu prefix data"sv;
  }
  if(encoder.hevc[encoder_t::PASSED] && !encoder.hevc[encoder_t::NALU_PREFIX_5b]) {
    BOOST_LOG(warning) << encoder.name << ": hevc: replacing nalu prefix data"sv;
  }

  fg.disable();
  return true;
}

int init() {
  BOOST_LOG(info) << "//////////////////////////////////////////////////////////////////"sv;
  BOOST_LOG(info) << "//                                                              //"sv;
  BOOST_LOG(info) << "//   Testing for available encoders, this may generate errors.  //"sv;
  BOOST_LOG(info) << "//   You can safely ignore those errors.                        //"sv;
  BOOST_LOG(info) << "//                                                              //"sv;
  BOOST_LOG(info) << "//////////////////////////////////////////////////////////////////"sv;

  KITTY_WHILE_LOOP(auto pos = std::begin(encoders), pos != std::end(encoders), {
    if(
      (!config::video.encoder.empty() && pos->name != config::video.encoder) ||
      !validate_encoder(*pos) ||
      (config::video.hevc_mode == 3 && !pos->hevc[encoder_t::DYNAMIC_RANGE])) {
      pos = encoders.erase(pos);

      continue;
    }

    break;
  })

  BOOST_LOG(info);
  BOOST_LOG(info) << "//////////////////////////////////////////////////////////////"sv;
  BOOST_LOG(info) << "//                                                          //"sv;
  BOOST_LOG(info) << "// Ignore any errors mentioned above, they are not relevant //"sv;
  BOOST_LOG(info) << "//                                                          //"sv;
  BOOST_LOG(info) << "//////////////////////////////////////////////////////////////"sv;
  BOOST_LOG(info);

  if(encoders.empty()) {
    if(config::video.encoder.empty()) {
      BOOST_LOG(fatal) << "Couldn't find any encoder"sv;
    }
    else {
      BOOST_LOG(fatal) << "Couldn't find any encoder matching ["sv << config::video.encoder << ']';
    }

    return -1;
  }

  auto &encoder = encoders.front();

  BOOST_LOG(debug) << "------  h264 ------"sv;
  for(int x = 0; x < encoder_t::MAX_FLAGS; ++x) {
    auto flag = (encoder_t::flag_e)x;
    BOOST_LOG(debug) << encoder_t::from_flag(flag) << (encoder.h264[flag] ? ": supported"sv : ": unsupported"sv);
  }
  BOOST_LOG(debug) << "-------------------"sv;

  if(encoder.hevc[encoder_t::PASSED]) {
    BOOST_LOG(debug) << "------  hevc ------"sv;
    for(int x = 0; x < encoder_t::MAX_FLAGS; ++x) {
      auto flag = (encoder_t::flag_e)x;
      BOOST_LOG(debug) << encoder_t::from_flag(flag) << (encoder.hevc[flag] ? ": supported"sv : ": unsupported"sv);
    }
    BOOST_LOG(debug) << "-------------------"sv;

    BOOST_LOG(info) << "Found encoder "sv << encoder.name << ": ["sv << encoder.h264.name << ", "sv << encoder.hevc.name << ']';
  }
  else {
    BOOST_LOG(info) << "Found encoder "sv << encoder.name << ": ["sv << encoder.h264.name << ']';
  }

  if(config::video.hevc_mode == 0) {
    config::video.hevc_mode = encoder.hevc[encoder_t::PASSED] ? (encoder.hevc[encoder_t::DYNAMIC_RANGE] ? 3 : 2) : 1;
  }

  return 0;
}

int hwframe_ctx(ctx_t &ctx, buffer_t &hwdevice, AVPixelFormat format) {
  buffer_t frame_ref { av_hwframe_ctx_alloc(hwdevice.get()) };

  auto frame_ctx               = (AVHWFramesContext *)frame_ref->data;
  frame_ctx->format            = ctx->pix_fmt;
  frame_ctx->sw_format         = format;
  frame_ctx->height            = ctx->height;
  frame_ctx->width             = ctx->width;
  frame_ctx->initial_pool_size = 0;

  if(auto err = av_hwframe_ctx_init(frame_ref.get()); err < 0) {
    return err;
  }

  ctx->hw_frames_ctx = av_buffer_ref(frame_ref.get());

  return 0;
}

// Linux only declaration
typedef int (*vaapi_make_hwdevice_ctx_fn)(platf::hwdevice_t *base, AVBufferRef **hw_device_buf);

util::Either<buffer_t, int> vaapi_make_hwdevice_ctx(platf::hwdevice_t *base) {
  buffer_t hw_device_buf;

  // If an egl hwdevice
  if(base->data) {
    if(((vaapi_make_hwdevice_ctx_fn)base->data)(base, &hw_device_buf)) {
      return -1;
    }

    return hw_device_buf;
  }

  auto render_device = config::video.adapter_name.empty() ? nullptr : config::video.adapter_name.c_str();

  auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VAAPI, render_device, nullptr, 0);
  if(status < 0) {
    char string[AV_ERROR_MAX_STRING_SIZE];
    BOOST_LOG(error) << "Failed to create a VAAPI device: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
    return -1;
  }

  return hw_device_buf;
}

util::Either<buffer_t, int> cuda_make_hwdevice_ctx(platf::hwdevice_t *base) {
  buffer_t hw_device_buf;

  auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 1 /* AV_CUDA_USE_PRIMARY_CONTEXT */);
  if(status < 0) {
    char string[AV_ERROR_MAX_STRING_SIZE];
    BOOST_LOG(error) << "Failed to create a CUDA device: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
    return -1;
  }

  return hw_device_buf;
}

#ifdef _WIN32
}

void do_nothing(void *) {}

namespace video {
util::Either<buffer_t, int> dxgi_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx) {
  buffer_t ctx_buf { av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA) };
  auto ctx = (AVD3D11VADeviceContext *)((AVHWDeviceContext *)ctx_buf->data)->hwctx;

  std::fill_n((std::uint8_t *)ctx, sizeof(AVD3D11VADeviceContext), 0);

  auto device = (ID3D11Device *)hwdevice_ctx->data;

  device->AddRef();
  ctx->device = device;

  ctx->lock_ctx = (void *)1;
  ctx->lock     = do_nothing;
  ctx->unlock   = do_nothing;

  auto err = av_hwdevice_ctx_init(ctx_buf.get());
  if(err) {
    char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
    BOOST_LOG(error) << "Failed to create FFMpeg hardware device context: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

    return err;
  }

  return ctx_buf;
}
#endif

int start_capture_async(capture_thread_async_ctx_t &capture_thread_ctx) {
  capture_thread_ctx.encoder_p = &encoders.front();
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
void end_capture_async(capture_thread_async_ctx_t &capture_thread_ctx) {
  capture_thread_ctx.capture_ctx_queue->stop();

  capture_thread_ctx.capture_thread.join();
}

int start_capture_sync(capture_thread_sync_ctx_t &ctx) {
  std::thread { &captureThreadSync }.detach();
  return 0;
}
void end_capture_sync(capture_thread_sync_ctx_t &ctx) {}

platf::mem_type_e map_dev_type(AVHWDeviceType type) {
  switch(type) {
  case AV_HWDEVICE_TYPE_D3D11VA:
    return platf::mem_type_e::dxgi;
  case AV_HWDEVICE_TYPE_VAAPI:
    return platf::mem_type_e::vaapi;
  case AV_HWDEVICE_TYPE_CUDA:
    return platf::mem_type_e::cuda;
  case AV_HWDEVICE_TYPE_NONE:
    return platf::mem_type_e::system;
  default:
    return platf::mem_type_e::unknown;
  }

  return platf::mem_type_e::unknown;
}

platf::pix_fmt_e map_pix_fmt(AVPixelFormat fmt) {
  switch(fmt) {
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

color_t make_color_matrix(float Cr, float Cb, float U_max, float V_max, float add_Y, float add_UV, const float2 &range_Y, const float2 &range_UV) {
  float Cg = 1.0f - Cr - Cb;

  float Cr_i = 1.0f - Cr;
  float Cb_i = 1.0f - Cb;

  float shift_y  = range_Y[0] / 256.0f;
  float shift_uv = range_UV[0] / 256.0f;

  float scale_y  = (range_Y[1] - range_Y[0]) / 256.0f;
  float scale_uv = (range_UV[1] - range_UV[0]) / 256.0f;
  return {
    { Cr, Cg, Cb, add_Y },
    { -(Cr * U_max / Cb_i), -(Cg * U_max / Cb_i), U_max, add_UV },
    { V_max, -(Cg * V_max / Cr_i), -(Cb * V_max / Cr_i), add_UV },
    { scale_y, shift_y },
    { scale_uv, shift_uv },
  };
}

color_t colors[] {
  make_color_matrix(0.299f, 0.114f, 0.436f, 0.615f, 0.0625, 0.5f, { 16.0f, 235.0f }, { 16.0f, 240.0f }),   // BT601 MPEG
  make_color_matrix(0.299f, 0.114f, 0.5f, 0.5f, 0.0f, 0.5f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),           // BT601 JPEG
  make_color_matrix(0.2126f, 0.0722f, 0.436f, 0.615f, 0.0625, 0.5f, { 16.0f, 235.0f }, { 16.0f, 240.0f }), // BT701 MPEG
  make_color_matrix(0.2126f, 0.0722f, 0.5f, 0.5f, 0.0f, 0.5f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),         // BT701 JPEG
};
} // namespace video
