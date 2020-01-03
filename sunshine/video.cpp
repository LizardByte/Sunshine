//
// Created by loki on 6/6/19.
//

#include <iostream>
#include <fstream>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include "platform/common.h"
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
using img_event_t = std::shared_ptr<safe::event_t<std::unique_ptr<platf::img_t>>>;

auto open_codec(ctx_t &ctx, AVCodec *codec, AVDictionary **options) {
  avcodec_open2(ctx.get(), codec, options);

  return util::fail_guard([&]() {
    avcodec_close(ctx.get());
  });
}

void encode(int64_t frame, ctx_t &ctx, sws_t &sws, frame_t &yuv_frame, platf::img_t &img, packet_queue_t &packets) {
  av_frame_make_writable(yuv_frame.get());

  const int linesizes[2] {
    (int)(img.width * sizeof(int)), 0
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
    fprintf(stderr, "error sending a frame for encoding\n");
    exit(1);
  }

  while (ret >= 0) {
    packet_t packet { av_packet_alloc() };

    ret = avcodec_receive_packet(ctx.get(), packet.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      return;
    else if (ret < 0) {
      fprintf(stderr, "error during encoding\n");
      exit(1);
    }

    packets->raise(std::move(packet));
  }
}

void encodeThread(
  img_event_t images,
  packet_queue_t packets,
  idr_event_t idr_events,
  config_t config) {
  int framerate = config.framerate;

  auto codec = avcodec_find_encoder(AV_CODEC_ID_H264);

  ctx_t ctx{avcodec_alloc_context3(codec)};

  frame_t yuv_frame{av_frame_alloc()};

  ctx->width = config.width;
  ctx->height = config.height;
  ctx->time_base = AVRational{1, framerate};
  ctx->framerate = AVRational{framerate, 1};
  ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->max_b_frames = config::video.max_b_frames;
  ctx->has_b_frames = 1;

  ctx->slices = config.slicesPerFrame;
  ctx->thread_type = FF_THREAD_SLICE;
  ctx->thread_count = config::video.threads;


  AVDictionary *options {nullptr};
  av_dict_set(&options, "profile", config::video.profile.c_str(), 0);
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
    ctx->gop_size = config::video.gop_size;
    av_dict_set_int(&options, "crf", config::video.crf, 0);
  }
  else {
    ctx->gop_size = config::video.gop_size;
    av_dict_set_int(&options, "qp", config::video.qp, 0);
  }
  
  ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
  ctx->flags2 |= AV_CODEC_FLAG2_FAST;

  auto lg = open_codec(ctx, codec, &options);

  yuv_frame->format = ctx->pix_fmt;
  yuv_frame->width = ctx->width;
  yuv_frame->height = ctx->height;

  av_frame_get_buffer(yuv_frame.get(), 0);

  int64_t frame = 0;
  int64_t key_frame = 0;

  auto img_width  = 0;
  auto img_height = 0;

  // Initiate scaling context with correct height and width
  sws_t sws;
  while (auto img = images->pop()) {
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
    }

    if(idr_events->peek()) {
      yuv_frame->pict_type = AV_PICTURE_TYPE_I;

      auto event = idr_events->pop();
      TUPLE_2D_REF(start, end, *event);

      frame = start;
      key_frame = end + 2;
    }
    else if(frame == key_frame) {
      yuv_frame->pict_type = AV_PICTURE_TYPE_I;
    }

    encode(frame++, ctx, sws, yuv_frame, *img, packets);
    
    yuv_frame->pict_type = AV_PICTURE_TYPE_NONE;
  }
}

void capture_display(packet_queue_t packets, idr_event_t idr_events, config_t config) {
  display_cursor = true;

  int framerate = config.framerate;

  img_event_t images {new img_event_t::element_type };

  std::thread encoderThread { &encodeThread, images, packets, idr_events, config };

  auto disp = platf::display();

  auto time_span = std::chrono::floor<std::chrono::nanoseconds>(1s) / framerate;
  while(packets->running()) {
    auto next_snapshot = std::chrono::steady_clock::now() + time_span;
    auto img = disp->snapshot(display_cursor);

    if(!img) {
      std::this_thread::sleep_until(next_snapshot);
      continue;
    }

    images->raise(std::move(img));
    img.reset();

    auto t = std::chrono::steady_clock::now();
    if(t > next_snapshot) {
      std::cout << "Taking snapshot took "sv << std::chrono::floor<std::chrono::milliseconds>(t - next_snapshot).count() << " milliseconds too long"sv << std::endl;
    }

    std::this_thread::sleep_until(next_snapshot);
  }

  images->stop();
  encoderThread.join();
}
}
