/**
 * @file src/video_probe.h
 * @brief Codec requirements and capability policies used by encoder probing.
 */
#pragma once

#include "video.h"

#include <optional>
#include <string_view>

namespace video::detail {
  /**
   * @brief Describe an unsupported explicitly requested codec feature.
   * @param codec Probed codec capabilities.
   * @param mode Requested mode; zero selects automatic detection and one disables the codec.
   * @return Missing feature description, empty for basic codec support, or nullopt when satisfied.
   */
  inline std::optional<std::string_view> unsupported_codec_requirement(const encoder_t::codec_t &codec, int mode) {
    switch (mode) {
      case 5:
        if (!codec[encoder_t::DYNAMIC_RANGE] && !codec[encoder_t::DYNAMIC_RANGE_YUV444]) {
          return "Main10 Rext10_444";
        }
        break;
      case 4:
        if (!codec[encoder_t::DYNAMIC_RANGE_YUV444]) {
          return "Rext10_444";
        }
        break;
      case 3:
        if (!codec[encoder_t::DYNAMIC_RANGE]) {
          return "Main10";
        }
        break;
      case 2:
        if (!codec[encoder_t::PASSED]) {
          return "";
        }
        break;
    }
    return std::nullopt;
  }

  /**
   * @brief Check both basic viability and any requested HDR constraint during automatic encoder selection.
   * @param codec Probed codec capabilities.
   * @param mode Requested codec mode.
   * @return Whether the codec can satisfy automatic selection requirements.
   */
  inline bool codec_meets_requirements(const encoder_t::codec_t &codec, int mode) {
    return (mode < 2 || codec[encoder_t::PASSED]) && !unsupported_codec_requirement(codec, mode);
  }

  /**
   * @brief Resolve automatic advertisement while preserving explicit codec modes.
   * @param codec Probed codec capabilities.
   * @param requested Requested codec mode.
   * @return Explicit requested mode, or disabled/basic/HDR mode inferred from capabilities.
   */
  inline int advertised_codec_mode(const encoder_t::codec_t &codec, int requested) {
    if (requested != 0) {
      return requested;
    }
    if (!codec[encoder_t::PASSED]) {
      return 1;
    }
    return 2 + (codec[encoder_t::DYNAMIC_RANGE] ? 1 : 0) + (codec[encoder_t::DYNAMIC_RANGE_YUV444] ? 2 : 0);
  }

  /**
   * @brief Decide whether to test HEVC or AV1 before selecting an encoder.
   * @param mode Requested codec mode.
   * @param h264_only Whether the encoder explicitly supports H.264 only.
   * @return Whether probing this codec is requested or automatic and permitted.
   */
  inline bool should_probe_codec(int mode, bool h264_only) {
    return mode >= 2 || (mode == 0 && !h264_only);
  }

  /**
   * @brief Build the existing 1080p probe configuration for a pixel mode.
   * @param video_format H.264, HEVC or AV1 protocol codec number.
   * @param hdr Whether the configuration requests HDR.
   * @param yuv444 Whether the configuration requests 4:4:4 chroma.
   * @return Configuration preserving the encoder probe's frame, bitrate and reference defaults.
   */
  inline config_t pixel_probe_config(int video_format, bool hdr, bool yuv444) {
    return {1920, 1080, 60, 6000, 1000, 1, 0, hdr ? 3 : 1, video_format, hdr ? 1 : 0, yuv444 ? 1 : 0, 0};
  }
}  // namespace video::detail
