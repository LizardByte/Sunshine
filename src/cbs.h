#ifndef SUNSHINE_CBS_H
#define SUNSHINE_CBS_H

#include "utility.h"

struct AVPacket;
struct AVCodecContext;

namespace cbs {

struct nal_t {
  util::buffer_t<std::uint8_t> _new;
  util::buffer_t<std::uint8_t> old;
};

struct hevc_t {
  nal_t vps;
  nal_t sps;
};

struct h264_t {
  nal_t sps;
};

hevc_t make_sps_hevc(const AVCodecContext *ctx, const AVPacket *packet);
h264_t make_sps_h264(const AVCodecContext *ctx, const AVPacket *packet);

/**
 * Check if SPS->VUI is present
 */
bool validate_sps(const AVPacket *packet, int codec_id);
} // namespace cbs

#endif