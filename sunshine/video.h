//
// Created by loki on 6/9/19.
//

#ifndef SUNSHINE_VIDEO_H
#define SUNSHINE_VIDEO_H

#include "input.h"
#include "platform/common.h"
#include "thread_safe.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVPacket;
namespace video {

struct packet_raw_t : public AVPacket {
  void init_packet() {
    pts             = AV_NOPTS_VALUE;
    dts             = AV_NOPTS_VALUE;
    pos             = -1;
    duration        = 0;
    flags           = 0;
    stream_index    = 0;
    buf             = nullptr;
    side_data       = nullptr;
    side_data_elems = 0;
  }

  template<class P>
  explicit packet_raw_t(P *user_data) : channel_data { user_data } {
    init_packet();
  }

  explicit packet_raw_t(std::nullptr_t) : channel_data { nullptr } {
    init_packet();
  }

  ~packet_raw_t() {
    av_packet_unref(this);
  }

  struct replace_t {
    std::string_view old;
    std::string_view _new;

    KITTY_DEFAULT_CONSTR_MOVE(replace_t)

    replace_t(std::string_view old, std::string_view _new) noexcept : old { std::move(old) }, _new { std::move(_new) } {}
  };

  std::vector<replace_t> *replacements;

  void *channel_data;
};

using packet_t = std::unique_ptr<packet_raw_t>;

struct config_t {
  int width;
  int height;
  int framerate;
  int bitrate;
  int slicesPerFrame;
  int numRefFrames;
  int encoderCscMode;
  int videoFormat;
  int dynamicRange;
};

using float4 = float[4];
using float3 = float[3];
using float2 = float[2];

struct __attribute__((__aligned__(16))) color_t {
  float4 color_vec_y;
  float4 color_vec_u;
  float4 color_vec_v;
  float2 range_y;
  float2 range_uv;
};

extern color_t colors[4];

void capture(
  safe::mail_t mail,
  config_t config,
  void *channel_data);

int init();
} // namespace video

#endif //SUNSHINE_VIDEO_H
