// Created by loki on 6/9/19.

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

struct packet_raw_t {
  void init_packet() {
    this->av_packet = av_packet_alloc();
  }

  template<class P>
  explicit packet_raw_t(P *user_data) : channel_data { user_data } {
    init_packet();
  }

  explicit packet_raw_t(std::nullptr_t) : channel_data { nullptr } {
    init_packet();
  }

  ~packet_raw_t() {
    av_packet_unref(this->av_packet);
  }

  struct replace_t {
    std::string_view old;
    std::string_view _new;

    KITTY_DEFAULT_CONSTR_MOVE(replace_t)

    replace_t(std::string_view old, std::string_view _new) noexcept : old { std::move(old) }, _new { std::move(_new) } {}
  };

  AVPacket *av_packet;
  std::vector<replace_t> *replacements;
  void *channel_data;
};

using packet_t = std::unique_ptr<packet_raw_t>;

struct hdr_info_raw_t {
  explicit hdr_info_raw_t(bool enabled) : enabled { enabled }, metadata {} {};
  explicit hdr_info_raw_t(bool enabled, const SS_HDR_METADATA &metadata) : enabled { enabled }, metadata { metadata } {};

  bool enabled;
  SS_HDR_METADATA metadata;
};

using hdr_info_t = std::unique_ptr<hdr_info_raw_t>;

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

extern color_t colors[6];

void capture(
  safe::mail_t mail,
  config_t config,
  void *channel_data);

int init();
} // namespace video

#endif // SUNSHINE_VIDEO_H
