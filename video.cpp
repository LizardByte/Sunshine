//
// Created by loki on 6/6/19.
//

#include <iostream>
#include <fstream>
#include <thread>

#include <platform/common.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include "config.h"
#include "video.h"

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

using ctx_t     = util::safe_ptr<AVCodecContext, free_ctx>;
using frame_t   = util::safe_ptr<AVFrame, free_frame>;

using sws_t = util::safe_ptr<SwsContext, sws_freeContext>;

auto open_codec(ctx_t &ctx, AVCodec *codec, AVDictionary **options) {
  avcodec_open2(ctx.get(), codec, options);

  return util::fail_guard([&]() {
    avcodec_close(ctx.get());
  });
}

void encode(int64_t frame, ctx_t &ctx, sws_t &sws, frame_t &yuv_frame, platf::img_t &img, std::shared_ptr<safe::queue_t<packet_t>> &packets) {
  av_frame_make_writable(yuv_frame.get());

  const int linesizes[2] {
    (int)(platf::img_width(img) * sizeof(int)), 0
  };

  auto data = platf::img_data(img);
  int ret = sws_scale(sws.get(), (uint8_t*const*)&data, linesizes, 0, platf::img_height(img), yuv_frame->data, yuv_frame->linesize);

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

    packets->push(std::move(packet));
  }
}

void encodeThread(
  std::shared_ptr<safe::queue_t<platf::img_t>> images,
  std::shared_ptr<safe::queue_t<packet_t>> packets, config_t config) {
  int framerate = config.framerate;

  auto codec = avcodec_find_encoder(AV_CODEC_ID_H264);

  ctx_t ctx{avcodec_alloc_context3(codec)};

  frame_t yuv_frame{av_frame_alloc()};

  ctx->width = config.width;
  ctx->height = config.height;
  ctx->bit_rate = config.bitrate;
  ctx->time_base = AVRational{1, framerate};
  ctx->framerate = AVRational{framerate, 1};
  ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->max_b_frames = config::video.max_b_frames;
  ctx->gop_size = config::video.gop_size;

  ctx->slices = config.slicesPerFrame;
  ctx->thread_type = FF_THREAD_SLICE;
  ctx->thread_count = std::min(config.slicesPerFrame, 4);

  AVDictionary *options {nullptr};
  av_dict_set(&options, "preset", "ultrafast", 0);
  // av_dict_set(&options, "tune", "fastdecode", 0);
  av_dict_set(&options, "profile", "baseline", 0);

  av_dict_set_int(&options, "crf", config::video.crf, 0);

  ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
  ctx->flags2 |= AV_CODEC_FLAG2_FAST;

  auto fromformat = AV_PIX_FMT_BGR0;
  auto lg = open_codec(ctx, codec, &options);

  yuv_frame->format = ctx->pix_fmt;
  yuv_frame->width = ctx->width;
  yuv_frame->height = ctx->height;

  av_frame_get_buffer(yuv_frame.get(), 0);

  int64_t frame = 1;

  // Initiate scaling context with correct height and width
  sws_t sws;
  if(auto img = images->pop()) {
    sws.reset(
      sws_getContext(
        platf::img_width(img), platf::img_height(img), fromformat,
        ctx->width, ctx->height, ctx->pix_fmt,
        SWS_LANCZOS | SWS_ACCURATE_RND,
        nullptr, nullptr, nullptr));
  }

  while (auto img = images->pop()) {
    encode(frame++, ctx, sws, yuv_frame, img, packets);
  }

  packets->stop();
}

void capture_display(std::shared_ptr<safe::queue_t<packet_t>> packets, config_t config) {
  int framerate = config.framerate;

  std::shared_ptr<safe::queue_t<platf::img_t>> images { new safe::queue_t<platf::img_t> };

  std::thread encoderThread { &encodeThread, images, packets, config };

  auto disp = platf::display();

  auto time_span = std::chrono::floor<std::chrono::nanoseconds>(1s) / framerate;
  while(packets->running()) {
    auto next_snapshot = std::chrono::steady_clock::now() + time_span;
    auto img = platf::snapshot(disp);

    images->push(std::move(img));
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
