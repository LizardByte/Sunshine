//
// Created by loki on 6/6/19.
//

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
    BOOST_LOG(fatal) << "Could not send a frame for encoding"sv;
    log_flush();
    std::abort();
  }

  while (ret >= 0) {
    packet_t packet { av_packet_alloc() };

    ret = avcodec_receive_packet(ctx.get(), packet.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return;
    }
    else if (ret < 0) {
      BOOST_LOG(fatal) << "Could not encode video packet"sv;
      log_flush();
      std::abort();
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

    encode(frame++, ctx, sws, yuv_frame, *img, packets);
    
    yuv_frame->pict_type = AV_PICTURE_TYPE_NONE;
  }
}

void capture_display(packet_queue_t packets, idr_event_t idr_events, config_t config) {
  display_cursor = true;

  int framerate = config.framerate;

  auto disp = platf::display();
  if(!disp) {
    packets->stop();
    return;
  }

  img_event_t images {new img_event_t::element_type };
  std::thread encoderThread { &encodeThread, images, packets, idr_events, config };

  auto time_span = std::chrono::floor<std::chrono::nanoseconds>(1s) / framerate;
  while(packets->running()) {
    auto next_snapshot = std::chrono::steady_clock::now() + time_span;

    auto img = disp->alloc_img();
    auto status = disp->snapshot(img.get(), display_cursor);

    switch(status) {
      case platf::capture_e::reinit: {
        // We try this twice, in case we still get an error on reinitialization
        for(int x = 0; x < 2; ++x) {
          disp.reset();
          disp = platf::display();

          if (disp) {
            break;
          }

          std::this_thread::sleep_for(200ms);
        }

        if (!disp) {
          packets->stop();
        }
        continue;
      }
      case platf::capture_e::timeout:
        std::this_thread::sleep_until(next_snapshot);
        continue;
      case platf::capture_e::error:
        packets->stop();
        continue;
      // Prevent warning during compilation
      case platf::capture_e::ok:
        break;
    }

    images->raise(std::move(img));
    std::this_thread::sleep_until(next_snapshot);
  }

  images->stop();
  encoderThread.join();
}
}
