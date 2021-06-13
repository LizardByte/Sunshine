#ifndef SUNSHINE_CBS_H
#define SUNSHINE_CBS_H

#include "utility.h"

struct AVPacket;
struct AVCodecContext;
namespace cbs {

util::buffer_t<std::uint8_t> read_sps(const AVPacket *packet, int codec_id);
util::buffer_t<std::uint8_t> make_sps_h264(const AVCodecContext *ctx);
util::buffer_t<std::uint8_t> make_sps_hevc(const AVCodecContext *ctx);

/**
 * Check if SPS->VUI is present
 */
bool validate_sps(const AVPacket *packet, int codec_id);
} // namespace cbs

#endif