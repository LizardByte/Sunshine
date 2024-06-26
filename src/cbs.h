/**
 * @file src/cbs.h
 * @brief Declarations for FFmpeg Coded Bitstream API.
 */
#pragma once

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

  hevc_t
  make_sps_hevc(const AVCodecContext *ctx, const AVPacket *packet);
  h264_t
  make_sps_h264(const AVCodecContext *ctx, const AVPacket *packet);

  /**
   * @brief Validates the Sequence Parameter Set (SPS) of a given packet.
   * @param packet The packet to validate.
   * @param codec_id The ID of the codec used (either AV_CODEC_ID_H264 or AV_CODEC_ID_H265).
   * @return True if the SPS->VUI is present in the active SPS of the packet, false otherwise.
   */
  bool
  validate_sps(const AVPacket *packet, int codec_id);
}  // namespace cbs
