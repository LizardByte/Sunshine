#ifndef SUNSHINE_CBS_H
#define SUNSHINE_CBS_H

#include "utility.h"

struct AVPacket;
struct AVCodecContext;
namespace cbs {

struct sps_hevc_t {
  util::buffer_t<std::uint8_t> vps;
  util::buffer_t<std::uint8_t> sps;
};

util::buffer_t<std::uint8_t> read_sps_h264(const AVPacket *packet);
sps_hevc_t read_sps_hevc(const AVPacket *packet);

util::buffer_t<std::uint8_t> make_sps_h264(const AVCodecContext *ctx);
sps_hevc_t make_sps_hevc(const AVCodecContext *ctx);

/**
 * Check if SPS->VUI is present
 */
bool validate_sps(const AVPacket *packet, int codec_id);
} // namespace cbs

#endif