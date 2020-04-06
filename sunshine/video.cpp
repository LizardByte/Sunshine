//
// Created by loki on 6/6/19.
//

#include <atomic>
#include <thread>
#include <bitset>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include "platform/common.h"
#include "round_robin.h"
#include "sync.h"
#include "config.h"
#include "video.h"
#include "main.h"

namespace video {
using namespace std::literals;

void free_ctx(AVCodecContext *ctx) {
  avcodec_free_context(&ctx);
}

void free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}

void free_buffer(AVBufferRef *ref) {
  av_buffer_unref(&ref);
}

void free_packet(AVPacket *packet) {
  av_packet_free(&packet);
}

using ctx_t       = util::safe_ptr<AVCodecContext, free_ctx>;
using codec_t     = util::safe_ptr_v2<AVCodecContext, int, avcodec_close>;
using frame_t     = util::safe_ptr<AVFrame, free_frame>;
using buffer_t    = util::safe_ptr<AVBufferRef, free_buffer>;
using sws_t       = util::safe_ptr<SwsContext, sws_freeContext>;
using img_event_t = std::shared_ptr<safe::event_t<std::shared_ptr<platf::img_t>>>;

void sw_img_to_frame(sws_t &sws, const platf::img_t &img, frame_t &frame);

void nv_d3d_img_to_frame(sws_t &sws, const platf::img_t &img, frame_t &frame);
util::Either<buffer_t, int> nv_d3d_make_hwdevice_ctx(platf::hwdevice_ctx_t *hwdevice_ctx);

struct encoder_t {
  enum flag_e {
    PASSED, // Is supported
    REF_FRAMES_RESTRICT, // Set maximum reference frames
    REF_FRAMES_AUTOSELECT, // Allow encoder to select maximum reference frames (If !REF_FRAMES_RESTRICT --> REF_FRAMES_AUTOSELECT)
    MAX_FLAGS
  };

  struct option_t {
    std::string name;
    std::variant<int, int*, std::string, std::string*> value;
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
    std::string name;
    std::bitset<MAX_FLAGS> capabilities;

    std::bitset<MAX_FLAGS>::reference operator[](flag_e flag) {
      return capabilities[(std::size_t)flag];
    }
  } hevc, h264;

  bool system_memory;

  std::function<void(sws_t &, const platf::img_t&, frame_t&)> img_to_frame;
  std::function<util::Either<buffer_t, int>(platf::hwdevice_ctx_t *hwdevice)> make_hwdevice_ctx;
};

struct session_t {
  buffer_t hwdevice;

  ctx_t ctx;
  codec_t codec_handle;

  frame_t frame;

  AVPixelFormat sw_format;
  int sws_color_format;
};

static encoder_t nvenc {
  { 2, 0, 1 },
  AV_HWDEVICE_TYPE_D3D11VA,
  AV_PIX_FMT_D3D11,
  AV_PIX_FMT_NV12, AV_PIX_FMT_NV12,
  {
    { {"force-idr"s, 1} }, "hevc_nvenc"s
  },
  {
    { {"force-idr"s, 1} }, "h264_nvenc"s
  },
  false,

  nv_d3d_img_to_frame,
  nv_d3d_make_hwdevice_ctx
};

static encoder_t software {
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
      { "x265-params"s, "info=0:keyint=-1"s },
      { "preset"s, &config::video.preset },
      { "tune"s, &config::video.tune }
    }, "libx265"s
  },
  {
    {
      { "preset"s, &config::video.preset },
      { "tune"s, &config::video.tune }
    }, "libx264"s
  },
  true,

  sw_img_to_frame,
  nullptr
};

static std::vector<encoder_t> encoders {
  nvenc, software
};

struct capture_ctx_t {
  img_event_t images;
  std::chrono::nanoseconds delay;
};

struct capture_thread_ctx_t {
  std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue;
  std::thread capture_thread;

  safe::signal_t reinit_event;
  const encoder_t *encoder_p;
  util::sync_t<std::weak_ptr<platf::display_t>> display_wp;
};

[[nodiscard]] codec_t open_codec(ctx_t &ctx, AVCodec *codec, AVDictionary **options) {
  avcodec_open2(ctx.get(), codec, options);

  return codec_t { ctx.get() };
}

void reset_display(std::shared_ptr<platf::display_t> &disp, AVHWDeviceType type) {
  // We try this twice, in case we still get an error on reinitialization
  for(int x = 0; x < 2; ++x) {
    disp.reset();
    disp = platf::display(type);
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
  const encoder_t &encoder
  ) {
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

  std::chrono::nanoseconds delay = 1s;

  auto disp = platf::display(encoder.dev_type);
  if(!disp) {
    return;
  }
  display_wp = disp;

  std::vector<std::shared_ptr<platf::img_t>> imgs(12);
  auto round_robin = util::make_round_robin<std::shared_ptr<platf::img_t>>(std::begin(imgs) +1, std::end(imgs));

  int dummy_data = 0;
  for(auto &img : imgs) {
    img = disp->alloc_img();
    if(!img) {
      BOOST_LOG(error) << "Couldn't initialize an image"sv;
      return;
    }
  }
  auto &dummy_img = imgs.front();
  if(disp->dummy_img(dummy_img.get(), dummy_data)) {
    return;
  }

  auto next_frame = std::chrono::steady_clock::now();
  while(capture_ctx_queue->running()) {
    while(capture_ctx_queue->peek()) {
      capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));

      // Temporary image to ensure something is send to Moonlight even if no frame has been captured yet.
      capture_ctxs.back().images->raise(dummy_img);

      delay = std::min(delay, capture_ctxs.back().delay);
    }

    auto now = std::chrono::steady_clock::now();
    if(next_frame > now) {
      std::this_thread::sleep_until(next_frame);
    }
    next_frame += delay;

    auto &img = *round_robin++;
    platf::capture_e status;
    {
      auto lg = display_wp.lock();
      status = disp->snapshot(img.get(), display_cursor);
    }
    switch (status) {
      case platf::capture_e::reinit: {
        reinit_event.raise(true);

        // Some classes of images contain references to the display --> display won't delete unless img is deleted
        for(auto &img : imgs) {
          img.reset();
        }

        // Some classes of display cannot have multiple instances at once
        disp.reset();

        // display_wp is modified in this thread only
        while(!display_wp->expired()) {
          std::this_thread::sleep_for(100ms);
        }

        reset_display(disp, encoder.dev_type);
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
        if(disp->dummy_img(dummy_img.get(), dummy_data)) {
          return;
        }

        reinit_event.reset();
        continue;
      }
      case platf::capture_e::error:
       return;
      case platf::capture_e::timeout:
        continue;
      case platf::capture_e::ok:
        break;
      default:
        BOOST_LOG(error) << "Unrecognized capture status ["sv << (int)status << ']';
        return;
    }

    KITTY_WHILE_LOOP(auto capture_ctx = std::begin(capture_ctxs), capture_ctx != std::end(capture_ctxs), {
      if(!capture_ctx->images->running()) {
        auto tmp_delay = capture_ctx->delay;
        capture_ctx = capture_ctxs.erase(capture_ctx);

        if(tmp_delay == delay) {
          delay = std::min_element(std::begin(capture_ctxs), std::end(capture_ctxs), [](const auto &l, const auto &r) {
            return l.delay < r.delay;
          })->delay;
        }
        continue;
      }

      capture_ctx->images->raise(img);
      ++capture_ctx;
    })
  }
}

util::Either<buffer_t, int> hwdevice_ctx(AVHWDeviceType type, void *hwdevice_ctx) {
  buffer_t ctx;

  int err;
  if(hwdevice_ctx) {
    ctx.reset(av_hwdevice_ctx_alloc(type));
    ((AVHWDeviceContext*)ctx.get())->hwctx = hwdevice_ctx;

    err = av_hwdevice_ctx_init(ctx.get());
  }
  else {
    AVBufferRef *ref  {};
    err = av_hwdevice_ctx_create(&ref, type, nullptr, nullptr, 0);
    ctx.reset(ref);
  }

  if(err < 0) {
    return err;
  }

  return ctx;
}

int hwframe_ctx(ctx_t &ctx, buffer_t &hwdevice, AVPixelFormat format) {
  buffer_t frame_ref { av_hwframe_ctx_alloc(hwdevice.get())};

  auto frame_ctx = (AVHWFramesContext*)frame_ref->data;
  frame_ctx->format    = ctx->pix_fmt;
  frame_ctx->sw_format = format;
  frame_ctx->height    = ctx->height;
  frame_ctx->width     = ctx->width;
  frame_ctx->initial_pool_size = 0;

  if(auto err = av_hwframe_ctx_init(frame_ref.get()); err < 0) {
    return err;
  }

  ctx->hw_frames_ctx = av_buffer_ref(frame_ref.get());

  return 0;
}

int encode(int64_t frame_nr, ctx_t &ctx, frame_t &frame, packet_queue_t &packets, void *channel_data) {
  frame->pts = frame_nr;

  /* send the frame to the encoder */
  auto ret = avcodec_send_frame(ctx.get(), frame.get());
  if (ret < 0) {
    char err_str[AV_ERROR_MAX_STRING_SIZE] {0};
    BOOST_LOG(error) << "Could not send a frame for encoding: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, ret);

    return -1;
  }

  while (ret >= 0) {
    auto packet = std::make_unique<packet_t::element_type>(nullptr);

    ret = avcodec_receive_packet(ctx.get(), packet.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return 0;
    }
    else if (ret < 0) {
      return ret;
    }

    packet->channel_data = channel_data;
    packets->raise(std::move(packet));
  }

  return 0;
}

int start_capture(capture_thread_ctx_t &capture_thread_ctx) {
  capture_thread_ctx.encoder_p = &software;
  capture_thread_ctx.reinit_event.reset();

  capture_thread_ctx.capture_ctx_queue = std::make_shared<safe::queue_t<capture_ctx_t>>();

  capture_thread_ctx.capture_thread = std::thread {
    captureThread,
    capture_thread_ctx.capture_ctx_queue,
    std::ref(capture_thread_ctx.display_wp),
    std::ref(capture_thread_ctx.reinit_event),
    std::ref(*capture_thread_ctx.encoder_p)
  };

  return 0;
}
void end_capture(capture_thread_ctx_t &capture_thread_ctx) {
  capture_thread_ctx.capture_ctx_queue->stop();

  capture_thread_ctx.capture_thread.join();
}

std::optional<session_t>  make_session(const encoder_t &encoder, const config_t &config, platf::hwdevice_ctx_t *device_ctx) {
  bool hardware = encoder.dev_type != AV_HWDEVICE_TYPE_NONE;

  auto &video_format = config.videoFormat == 0 ? encoder.h264 : encoder.hevc;

  auto codec = avcodec_find_encoder_by_name(video_format.name.c_str());
  if(!codec) {
    BOOST_LOG(error) << "Couldn't open ["sv << video_format.name << ']';

    return std::nullopt;
  }

  ctx_t ctx {avcodec_alloc_context3(codec) };
  ctx->width = config.width;
  ctx->height = config.height;
  ctx->time_base = AVRational{1, config.framerate};
  ctx->framerate = AVRational{config.framerate, 1};

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
  ctx->gop_size = std::numeric_limits<int>::max();
  ctx->keyint_min = ctx->gop_size;

  // Some client decoders have limits on the number of reference frames
  ctx->refs = config.numRefFrames;

  ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
  ctx->flags2 |= AV_CODEC_FLAG2_FAST;

  ctx->color_range = (config.encoderCscMode & 0x1) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

  int sws_color_space;
  switch (config.encoderCscMode >> 1) {
    case 0:
    default:
      // Rec. 601
      ctx->color_primaries = AVCOL_PRI_SMPTE170M;
      ctx->color_trc = AVCOL_TRC_SMPTE170M;
      ctx->colorspace = AVCOL_SPC_SMPTE170M;
      sws_color_space = SWS_CS_SMPTE170M;
      break;

    case 1:
      // Rec. 709
      ctx->color_primaries = AVCOL_PRI_BT709;
      ctx->color_trc = AVCOL_TRC_BT709;
      ctx->colorspace = AVCOL_SPC_BT709;
      sws_color_space = SWS_CS_ITU709;
      break;

    case 2:
      // Rec. 2020
      ctx->color_primaries = AVCOL_PRI_BT2020;
      ctx->color_trc = AVCOL_TRC_BT2020_10;
      ctx->colorspace = AVCOL_SPC_BT2020_NCL;
      sws_color_space = SWS_CS_BT2020;
      break;
  }

  AVPixelFormat sw_fmt;
  if(config.dynamicRange == 0) {
    sw_fmt = encoder.static_pix_fmt;
  }
  else {
    sw_fmt = encoder.dynamic_pix_fmt;
  }

  buffer_t hwdevice;
  if(hardware) {
    ctx->pix_fmt = encoder.dev_pix_fmt;

    auto buf_or_error = encoder.make_hwdevice_ctx(device_ctx);
    if(buf_or_error.has_right()) {
      return std::nullopt;
    }

    hwdevice = std::move(buf_or_error.left());
    if(hwframe_ctx(ctx, hwdevice, sw_fmt)) {
      return std::nullopt;
    }
  }
  else /* software */ {
    ctx->pix_fmt = sw_fmt;

    // Clients will request for the fewest slices per frame to get the
    // most efficient encode, but we may want to provide more slices than
    // requested to ensure we have enough parallelism for good performance.
    ctx->slices = std::max(config.slicesPerFrame, config::video.min_threads);
    ctx->thread_type = FF_THREAD_SLICE;
    ctx->thread_count = ctx->slices;
  }

  AVDictionary *options {nullptr};
  for(auto &option : video_format.options) {
    std::visit(util::overloaded {
      [&](int v) { av_dict_set_int(&options, option.name.c_str(), v, 0); },
      [&](int *v) { av_dict_set_int(&options, option.name.c_str(), *v, 0); },
      [&](const std::string &v) { av_dict_set(&options, option.name.c_str(), v.c_str(), 0); },
      [&](std::string *v) { av_dict_set(&options, option.name.c_str(), v->c_str(), 0); }
    }, option.value);
  }

  if(config.bitrate > 500) {
    auto bitrate = config.bitrate * 1000;
    ctx->rc_max_rate = bitrate;
    ctx->rc_buffer_size = bitrate / 100;
    ctx->bit_rate = bitrate;
    ctx->rc_min_rate = bitrate;
  }
  else if(config::video.crf != 0) {
    av_dict_set_int(&options, "crf", config::video.crf, 0);
  }
  else {
    av_dict_set_int(&options, "qp", config::video.qp, 0);
  }

  auto codec_handle = open_codec(ctx, codec, &options);

  frame_t frame {av_frame_alloc() };
  frame->format = ctx->pix_fmt;
  frame->width = ctx->width;
  frame->height = ctx->height;


  if(hardware) {
    frame->hw_frames_ctx = av_buffer_ref(ctx->hw_frames_ctx);
  }
  else /* software */ {
    av_frame_get_buffer(frame.get(), 0);
  }

  return std::make_optional(session_t {
    std::move(hwdevice),
    std::move(ctx),
    std::move(codec_handle),
    std::move(frame),
    sw_fmt,
    sws_color_space
  });
}

void encode_run(
  int &frame_nr, int &key_frame_nr, // Store progress of the frame number
  safe::signal_t* shutdown_event, // Signal for shutdown event of the session
  packet_queue_t packets,
  idr_event_t idr_events,
  img_event_t images,
  config_t config,
  platf::hwdevice_ctx_t *hwdevice_ctx,
  safe::signal_t &reinit_event,
  const encoder_t &encoder,
  void *channel_data) {

  auto session = make_session(encoder, config, hwdevice_ctx);
  if(!session) {
    return;
  }

  auto delay = std::chrono::floor<std::chrono::nanoseconds>(1s) / config.framerate;

  auto img_width  = 0;
  auto img_height = 0;

  // Initiate scaling context with correct height and width
  sws_t sws;

  auto next_frame = std::chrono::steady_clock::now();
  while(true) {
    if(shutdown_event->peek() || reinit_event.peek() || !images->running()) {
      break;
    }

    if(idr_events->peek()) {
      session->frame->pict_type = AV_PICTURE_TYPE_I;

      auto event = idr_events->pop();
      TUPLE_2D_REF(_, end, *event);

      frame_nr = end;
      key_frame_nr = end + config.framerate;
    }
    else if(frame_nr == key_frame_nr) {
      session->frame->pict_type = AV_PICTURE_TYPE_I;
    }

    std::this_thread::sleep_until(next_frame);
    next_frame += delay;

    // When Moonlight request an IDR frame, send frames even if there is no new captured frame
    if(frame_nr > (key_frame_nr + config.framerate) || images->peek()) {
      if(auto img = images->pop(delay)) {
        if(encoder.system_memory) {
          auto new_width  = img->width;
          auto new_height = img->height;

          if(img_width != new_width || img_height != new_height) {
            img_width  = new_width;
            img_height = new_height;

            sws.reset(
              sws_getContext(
                img_width, img_height, AV_PIX_FMT_BGR0,
                session->ctx->width, session->ctx->height, session->sw_format,
                SWS_LANCZOS | SWS_ACCURATE_RND,
                nullptr, nullptr, nullptr));

            sws_setColorspaceDetails(sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 0,
                                     sws_getCoefficients(session->sws_color_format), config.encoderCscMode & 0x1,
                                     0, 1 << 16, 1 << 16);
          }

          encoder.img_to_frame(sws, *img, session->frame);
        }
        else {
          auto converted_img = hwdevice_ctx->convert(*img);
          if(!converted_img) {
            return;
          }
 
          encoder.img_to_frame(sws, *converted_img, session->frame);

        }
      }
      else if(images->running()) {
        continue;
      }
      else {
        break;
      }
    }

    if(encode(frame_nr++, session->ctx, session->frame, packets, channel_data)) {
      BOOST_LOG(fatal) << "Could not encode video packet"sv;
      log_flush();
      std::abort();
    }

    session->frame->pict_type = AV_PICTURE_TYPE_NONE;
  }
}

void capture(
  safe::signal_t *shutdown_event,
  packet_queue_t packets,
  idr_event_t idr_events,
  config_t config,
  void *channel_data) {

  auto images = std::make_shared<img_event_t::element_type>();
  auto lg = util::fail_guard([&]() {
    images->stop();
    shutdown_event->raise(true);
  });

  // Keep a reference counter to ensure the Fcapture thread only runs when other threads have a reference to the capture thread
  static auto capture_thread = safe::make_shared<capture_thread_ctx_t>(start_capture, end_capture);
  auto ref = capture_thread.ref();
  if(!ref) {
    return;
  }

  auto delay = std::chrono::floor<std::chrono::nanoseconds>(1s) / config.framerate;
  ref->capture_ctx_queue->raise(capture_ctx_t {
    images, delay
  });

  if(!ref->capture_ctx_queue->running()) {
    return;
  }

  int frame_nr = 1;
  int key_frame_nr = 1;
  while(!shutdown_event->peek() && images->running()) {
    // Wait for the display to be ready
    std::shared_ptr<platf::hwdevice_ctx_t> hwdevice_ctx;
    {
      auto lg = ref->display_wp.lock();
      if(ref->display_wp->expired()) {
        continue;
      }

      auto display = ref->display_wp->lock();

      auto pix_fmt = config.dynamicRange == 0 ? platf::pix_fmt_e::yuv420p : platf::pix_fmt_e::yuv420p10;
      hwdevice_ctx = display->make_hwdevice_ctx(config.width, config.height, pix_fmt);
      if(!hwdevice_ctx) {
        return;
      }
    }

    encode_run(frame_nr, key_frame_nr, shutdown_event, packets, idr_events, images, config, hwdevice_ctx.get(), ref->reinit_event, *ref->encoder_p, channel_data);
  }
}

bool validate_config(std::shared_ptr<platf::display_t> &disp, const encoder_t &encoder, const config_t &config) {
  reset_display(disp, encoder.dev_type);
  if(!disp) {
    return false;
  }


  auto pix_fmt = config.dynamicRange == 0 ? platf::pix_fmt_e::yuv420p : platf::pix_fmt_e::yuv420p10;
  auto hwdevice_ctx = disp->make_hwdevice_ctx(config.width, config.height, pix_fmt);
  if(!hwdevice_ctx) {
    return false;
  }

  auto session = make_session(encoder, config, hwdevice_ctx.get());
  if(!session) {
    return false;
  }

  int dummy_data;
  auto img = disp->alloc_img();
  if(disp->dummy_img(img.get(), dummy_data)) {
    return false;
  }

  sws_t sws;
  if(encoder.system_memory) {
    sws.reset(sws_getContext(
      img->width, img->height, AV_PIX_FMT_BGR0,
      session->ctx->width, session->ctx->height, session->sw_format,
      SWS_LANCZOS | SWS_ACCURATE_RND,
      nullptr, nullptr, nullptr));

    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 0,
                             sws_getCoefficients(session->sws_color_format), config.encoderCscMode & 0x1,
                             0, 1 << 16, 1 << 16);

    encoder.img_to_frame(sws, *img, session->frame);
  }
  else {
    auto converted_img = hwdevice_ctx->convert(*img);
    if(!converted_img) {
      return false;
    }

    encoder.img_to_frame(sws, *converted_img, session->frame);
  }

  session->frame->pict_type = AV_PICTURE_TYPE_I;

  auto packets = std::make_shared<packet_queue_t::element_type>();
  if(encode(1, session->ctx, session->frame, packets, nullptr)) {
    return false;
  }

  return true;
}

bool validate_encoder(encoder_t &encoder) {
  std::shared_ptr<platf::display_t> disp;
  // First, test encoder viability
  config_t config_max_ref_frames {
    1920, 1080,
    60,
    1000,
    1,
    1,
    1,
    0,
    0
  };

  config_t config_autoselect {
    1920, 1080,
    60,
    1000,
    1,
    0,
    1,
    0,
    0
  };

  auto max_ref_frames_h264 = validate_config(disp, encoder, config_max_ref_frames);
  auto autoselect_h264     = validate_config(disp, encoder, config_autoselect);

  if(!max_ref_frames_h264 && !autoselect_h264) {
    return false;
  }

  auto max_ref_frames_hevc = validate_config(disp, encoder, config_max_ref_frames);
  auto autoselect_hevc     = validate_config(disp, encoder, config_autoselect);

  encoder.h264[encoder_t::REF_FRAMES_RESTRICT] = max_ref_frames_h264;
  encoder.h264[encoder_t::REF_FRAMES_AUTOSELECT] = autoselect_h264;
  encoder.h264[encoder_t::PASSED] = true;
  encoder.hevc[encoder_t::REF_FRAMES_RESTRICT] = max_ref_frames_hevc;
  encoder.hevc[encoder_t::REF_FRAMES_AUTOSELECT] = autoselect_hevc;
  encoder.hevc[encoder_t::PASSED] = max_ref_frames_hevc || autoselect_hevc;

  std::vector<std::pair<encoder_t::flag_e, config_t>> configs; 
  for(auto &[flag, config] : configs) {
    auto h264 = config;
    auto hevc = config;

    h264.videoFormat = 0;
    hevc.videoFormat = 1;

    if(validate_config(disp, encoder, h264)) {
      encoder.h264[flag] = true;
    }

    if(validate_config(disp, encoder, hevc)) {
      encoder.hevc[flag] = true;
    }
  }
  
  return true;
}

void init() {
  KITTY_WHILE_LOOP(auto pos = std::begin(encoders), pos != std::end(encoders), {
    if(!validate_encoder(*pos)) {
      pos = encoders.erase(pos);

      continue;
    }

    ++pos;
  })

  for(auto &encoder : encoders) {
    BOOST_LOG(info) << "Found encoder ["sv << encoder.h264.name << ", "sv << encoder.hevc.name << ']';
  }
}

void sw_img_to_frame(sws_t &sws, const platf::img_t &img, frame_t &frame) {
  av_frame_make_writable(frame.get());

  const int linesizes[2] {
    img.row_pitch, 0
  };

  int ret = sws_scale(sws.get(), (std::uint8_t*const*)&img.data, linesizes, 0, img.height, frame->data, frame->linesize);
  if(ret <= 0) {
    BOOST_LOG(fatal) << "Couldn't convert image to required format and/or size"sv;

    log_flush();
    std::abort();
  }
}

void nv_d3d_img_to_frame(sws_t &sws, const platf::img_t &img, frame_t &frame) {
  // Need to have something refcounted
  if(!frame->buf[0]) {
    frame->buf[0] = av_buffer_allocz(sizeof(AVD3D11FrameDescriptor*));
  }

  auto desc = (AVD3D11FrameDescriptor*)frame->buf[0]->data;
  desc->texture = (ID3D11Texture2D*)img.data;
  desc->index = 0;

  frame->data[0] = img.data;
  frame->data[1] = 0;

  frame->linesize[0] = img.row_pitch;

  frame->height = img.height;
  frame->width = img.width;
}

util::Either<buffer_t, int> nv_d3d_make_hwdevice_ctx(platf::hwdevice_ctx_t *hwdevice_ctx) {
  buffer_t ctx_buf { av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA) };
  auto ctx = (AVD3D11VADeviceContext*)((AVHWDeviceContext*)ctx_buf->data)->hwctx;

  std::fill_n((std::uint8_t*)ctx, sizeof(AVD3D11VADeviceContext), 0);
  std::swap(ctx->device, *(ID3D11Device**)&hwdevice_ctx->hwdevice);

  auto err = av_hwdevice_ctx_init(ctx_buf.get());
  if(err) {
    char err_str[AV_ERROR_MAX_STRING_SIZE] {0};
    BOOST_LOG(error) << "Failed to create FFMpeg nvenc: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

    return err;
  }

  return ctx_buf;
}
}
