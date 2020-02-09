//
// Created by loki on 6/6/19.
//

#include <atomic>
#include <thread>

extern "C" {
#include <libswscale/swscale.h>
}

#include "platform/common.h"
#include "thread_pool.h"
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

void free_packet(AVPacket *packet) {
  av_packet_free(&packet);
}

using ctx_t       = util::safe_ptr<AVCodecContext, free_ctx>;
using frame_t     = util::safe_ptr<AVFrame, free_frame>;
using sws_t       = util::safe_ptr<SwsContext, sws_freeContext>;
using img_event_t = std::shared_ptr<safe::event_t<std::shared_ptr<platf::img_t>>>;

struct capture_ctx_t {
  img_event_t images;
  std::chrono::nanoseconds delay;
  std::chrono::steady_clock::time_point next_frame;
};

struct capture_thread_ctx_t {
  std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue;
  std::thread capture_thread;
};

[[nodiscard]] auto open_codec(ctx_t &ctx, AVCodec *codec, AVDictionary **options) {
  avcodec_open2(ctx.get(), codec, options);

  return util::fail_guard([&]() {
    avcodec_close(ctx.get());
  });
}

int capture_display(platf::img_t *img, std::unique_ptr<platf::display_t> &disp) {
  auto status = disp->snapshot(img, display_cursor);
  switch (status) {
    case platf::capture_e::reinit: {
      // We try this twice, in case we still get an error on reinitialization
      for(int x = 0; x < 2; ++x) {
        disp.reset();
        disp = platf::display();

        if(disp) {
          break;
        }

        std::this_thread::sleep_for(200ms);
      }

      if(!disp) {
        return -1;
      }

      return 0;
    }
    case platf::capture_e::error:
     return -1;
     // Prevent warning during compilation
    case platf::capture_e::timeout:
    case platf::capture_e::ok:
      return 1;
    default:
      BOOST_LOG(error) << "Unrecognized capture status ["sv << (int)status << ']';
      return -1;
  }
}

void captureThread(std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue) {
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

  auto disp = platf::display();
  while(capture_ctx_queue->running()) {
    while(capture_ctx_queue->peek()) {
      capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));
    }

    std::shared_ptr<platf::img_t> img = disp->alloc_img();
    auto result = capture_display(img.get(), disp);
    if(result < 0) {
      return;
    }
    if(!result) {
      continue;
    }

    KITTY_WHILE_LOOP(auto time_point = std::chrono::steady_clock::now(); auto capture_ctx = std::begin(capture_ctxs), capture_ctx != std::end(capture_ctxs), {
      if(!capture_ctx->images->running()) {
        capture_ctx = capture_ctxs.erase(capture_ctx);
        continue;
      }

      if(time_point > capture_ctx->next_frame) {
        capture_ctx->images->raise(img);
        capture_ctx->next_frame = time_point + capture_ctx->delay;
      }

      ++capture_ctx;
    })
  }
}

void encode(int64_t frame, ctx_t &ctx, sws_t &sws, frame_t &yuv_frame, platf::img_t &img, packet_queue_t &packets, void *channel_data) {
  av_frame_make_writable(yuv_frame.get());

  const int linesizes[2] {
    img.row_pitch, 0
  };

  auto data = img.data;
  int ret = sws_scale(sws.get(), (uint8_t*const*)&data, linesizes, 0, img.height, yuv_frame->data, yuv_frame->linesize);

  if(ret <= 0) {
    exit(1);
  }

  yuv_frame->pts = frame;

  /* send the frame to the encoder */
  ret = avcodec_send_frame(ctx.get(), yuv_frame.get());
  if (ret < 0) {
    BOOST_LOG(fatal) << "Could not send a frame for encoding"sv;
    log_flush();
    std::abort();
  }

  while (ret >= 0) {
    auto packet = std::make_unique<packet_t::element_type>(nullptr);

    ret = avcodec_receive_packet(ctx.get(), packet.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return;
    }
    else if (ret < 0) {
      BOOST_LOG(fatal) << "Could not encode video packet"sv;
      log_flush();
      std::abort();
    }

    packet->channel_data = channel_data;
    packets->raise(std::move(packet));
  }
}

int start_capture(capture_thread_ctx_t &capture_thread_ctx) {
  capture_thread_ctx.capture_ctx_queue = std::make_shared<safe::queue_t<capture_ctx_t>>();

  capture_thread_ctx.capture_thread = std::thread {
    captureThread, capture_thread_ctx.capture_ctx_queue
  };

  return 0;
}
void end_capture(capture_thread_ctx_t &capture_thread_ctx) {
  capture_thread_ctx.capture_ctx_queue->stop();

  capture_thread_ctx.capture_thread.join();
}

void capture(
  safe::signal_t *shutdown_event,
  packet_queue_t packets,
  idr_event_t idr_events,
  config_t config,
  void *channel_data) {

  int framerate = config.framerate;

  auto images = std::make_shared<img_event_t::element_type>();
  // Keep a reference counter to ensure the capture thread only runs when other threads have a reference to the capture thread
  static auto capture_thread = safe::make_shared<capture_thread_ctx_t>(start_capture, end_capture);
  auto ref = capture_thread.ref();
  if(!ref) {
    return;
  }

  ref->capture_ctx_queue->raise(capture_ctx_t {
    images, std::chrono::floor<std::chrono::nanoseconds>(1s) / framerate, std::chrono::steady_clock::now()
  });

  if(!ref->capture_ctx_queue->running()) {
    return;
  }

  AVCodec *codec;

  if(config.videoFormat == 0) {
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  }
  else {
    codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
  }

  ctx_t ctx{avcodec_alloc_context3(codec)};

  frame_t yuv_frame{av_frame_alloc()};

  ctx->width = config.width;
  ctx->height = config.height;
  ctx->time_base = AVRational{1, framerate};
  ctx->framerate = AVRational{framerate, 1};

  if(config.videoFormat == 0) {
    ctx->profile = FF_PROFILE_H264_HIGH;
  }
  else if(config.dynamicRange == 0) {
    ctx->profile = FF_PROFILE_HEVC_MAIN;
  }
  else {
    ctx->profile = FF_PROFILE_HEVC_MAIN_10;
  }

  if(config.dynamicRange == 0) {
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  }
  else {
    ctx->pix_fmt = AV_PIX_FMT_YUV420P10;
  }

  ctx->color_range = (config.encoderCscMode & 0x1) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

  int swsColorSpace;
  switch (config.encoderCscMode >> 1) {
    case 0:
    default:
      // Rec. 601
      ctx->color_primaries = AVCOL_PRI_SMPTE170M;
      ctx->color_trc = AVCOL_TRC_SMPTE170M;
      ctx->colorspace = AVCOL_SPC_SMPTE170M;
      swsColorSpace = SWS_CS_SMPTE170M;
      break;

    case 1:
      // Rec. 709
      ctx->color_primaries = AVCOL_PRI_BT709;
      ctx->color_trc = AVCOL_TRC_BT709;
      ctx->colorspace = AVCOL_SPC_BT709;
      swsColorSpace = SWS_CS_ITU709;
      break;

    case 2:
      // Rec. 2020
      ctx->color_primaries = AVCOL_PRI_BT2020;
      ctx->color_trc = AVCOL_TRC_BT2020_10;
      ctx->colorspace = AVCOL_SPC_BT2020_NCL;
      swsColorSpace = SWS_CS_BT2020;
      break;
  }

  // B-frames delay decoder output, so never use them
  ctx->max_b_frames = 0;

  // Use an infinite GOP length since I-frames are generated on demand
  ctx->gop_size = std::numeric_limits<int>::max();
  ctx->keyint_min = ctx->gop_size;

  // Some client decoders have limits on the number of reference frames
  ctx->refs = config.numRefFrames;

  // Clients will request for the fewest slices per frame to get the
  // most efficient encode, but we may want to provide more slices than
  // requested to ensure we have enough parallelism for good performance.
  ctx->slices = std::max(config.slicesPerFrame, config::video.min_threads);
  ctx->thread_type = FF_THREAD_SLICE;
  ctx->thread_count = ctx->slices;

  AVDictionary *options {nullptr};
  av_dict_set(&options, "preset", config::video.preset.c_str(), 0);
  av_dict_set(&options, "tune", config::video.tune.c_str(), 0);

  if(config.bitrate > 500) {
    config.bitrate *= 1000;
    ctx->rc_max_rate = config.bitrate;
    ctx->rc_buffer_size = config.bitrate / 100;
    ctx->bit_rate = config.bitrate;
    ctx->rc_min_rate = config.bitrate;
  }
  else if(config::video.crf != 0) {
    av_dict_set_int(&options, "crf", config::video.crf, 0);
  }
  else {
    av_dict_set_int(&options, "qp", config::video.qp, 0);
  }

  if(config.videoFormat == 1) {
    // x265's Info SEI is so long that it causes the IDR picture data to be
    // kicked to the 2nd packet in the frame, breaking Moonlight's parsing logic.
    // It also looks like gop_size isn't passed on to x265, so we have to set
    // 'keyint=-1' in the parameters ourselves.
    av_dict_set(&options, "x265-params", "info=0:keyint=-1", 0);
  }
  
  ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
  ctx->flags2 |= AV_CODEC_FLAG2_FAST;

  auto lg = open_codec(ctx, codec, &options);

  yuv_frame->format = ctx->pix_fmt;
  yuv_frame->width = ctx->width;
  yuv_frame->height = ctx->height;

  av_frame_get_buffer(yuv_frame.get(), 0);

  int64_t frame = 1;
  int64_t key_frame = 1;

  auto img_width  = 0;
  auto img_height = 0;

  // Initiate scaling context with correct height and width
  sws_t sws;
  while(auto img = images->pop()) {
    if(shutdown_event->peek()) {
      break;
    }

    auto new_width  = img->width;
    auto new_height = img->height;

    if(img_width != new_width || img_height != new_height) {
      img_width  = new_width;
      img_height = new_height;

      sws.reset(
        sws_getContext(
          img_width, img_height, AV_PIX_FMT_BGR0,
          ctx->width, ctx->height, ctx->pix_fmt,
          SWS_LANCZOS | SWS_ACCURATE_RND,
          nullptr, nullptr, nullptr));

      sws_setColorspaceDetails(sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 0,
                               sws_getCoefficients(swsColorSpace), config.encoderCscMode & 0x1,
                               0, 1 << 16, 1 << 16);
    }

    if(idr_events->peek()) {
      yuv_frame->pict_type = AV_PICTURE_TYPE_I;

      auto event = idr_events->pop();
      TUPLE_2D_REF(_, end, *event);

      frame = end;
      key_frame = end + config.framerate;
    }
    else if(frame == key_frame) {
      yuv_frame->pict_type = AV_PICTURE_TYPE_I;
    }

    encode(frame++, ctx, sws, yuv_frame, *img, packets, channel_data);
    
    yuv_frame->pict_type = AV_PICTURE_TYPE_NONE;
  }

  images->stop();
}
}
