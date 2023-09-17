/**
 * @file src/video.h
 * @brief todo
 */
#pragma once

#include "input.h"
#include "platform/common.h"
#include "thread_safe.h"
#include "video_colorspace.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVPacket;
namespace video {

  struct packet_raw_t {
    virtual ~packet_raw_t() = default;

    virtual bool
    is_idr() = 0;

    virtual int64_t
    frame_index() = 0;

    virtual uint8_t *
    data() = 0;

    virtual size_t
    data_size() = 0;

    struct replace_t {
      std::string_view old;
      std::string_view _new;

      KITTY_DEFAULT_CONSTR_MOVE(replace_t)

      replace_t(std::string_view old, std::string_view _new) noexcept:
          old { std::move(old) }, _new { std::move(_new) } {}
    };

    std::vector<replace_t> *replacements = nullptr;
    void *channel_data = nullptr;
    bool after_ref_frame_invalidation = false;
    std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
  };

  struct packet_raw_avcodec: packet_raw_t {
    packet_raw_avcodec() {
      av_packet = av_packet_alloc();
    }

    ~packet_raw_avcodec() {
      av_packet_free(&this->av_packet);
    }

    bool
    is_idr() override {
      return av_packet->flags & AV_PKT_FLAG_KEY;
    }

    int64_t
    frame_index() override {
      return av_packet->pts;
    }

    uint8_t *
    data() override {
      return av_packet->data;
    }

    size_t
    data_size() override {
      return av_packet->size;
    }

    AVPacket *av_packet;
  };

  struct packet_raw_generic: packet_raw_t {
    packet_raw_generic(std::vector<uint8_t> &&frame_data, int64_t frame_index, bool idr):
        frame_data { std::move(frame_data) }, index { frame_index }, idr { idr } {
    }

    bool
    is_idr() override {
      return idr;
    }

    int64_t
    frame_index() override {
      return index;
    }

    uint8_t *
    data() override {
      return frame_data.data();
    }

    size_t
    data_size() override {
      return frame_data.size();
    }

    std::vector<uint8_t> frame_data;
    int64_t index;
    bool idr;
  };

  using packet_t = std::unique_ptr<packet_raw_t>;

  struct hdr_info_raw_t {
    explicit hdr_info_raw_t(bool enabled):
        enabled { enabled }, metadata {} {};
    explicit hdr_info_raw_t(bool enabled, const SS_HDR_METADATA &metadata):
        enabled { enabled }, metadata { metadata } {};

    bool enabled;
    SS_HDR_METADATA metadata;
  };

  using hdr_info_t = std::unique_ptr<hdr_info_raw_t>;

  /* Encoding configuration requested by remote client */
  struct config_t {
    int width;  // Video width in pixels
    int height;  // Video height in pixels
    int framerate;  // Requested framerate, used in individual frame bitrate budget calculation
    int bitrate;  // Video bitrate in kilobits (1000 bits) for requested framerate
    int slicesPerFrame;  // Number of slices per frame
    int numRefFrames;  // Max number of reference frames

    /* Requested color range and SDR encoding colorspace, HDR encoding colorspace is always BT.2020+ST2084
       Color range (encoderCscMode & 0x1) : 0 - limited, 1 - full
       SDR encoding colorspace (encoderCscMode >> 1) : 0 - BT.601, 1 - BT.709, 2 - BT.2020 */
    int encoderCscMode;

    int videoFormat;  // 0 - H.264, 1 - HEVC, 2 - AV1

    /* Encoding color depth (bit depth): 0 - 8-bit, 1 - 10-bit
       HDR encoding activates when color depth is higher than 8-bit and the display which is being captured is operating in HDR mode */
    int dynamicRange;
  };

  extern int active_hevc_mode;
  extern int active_av1_mode;
  extern bool last_encoder_probe_supported_ref_frames_invalidation;

  void
  capture(
    safe::mail_t mail,
    config_t config,
    void *channel_data);

  int
  probe_encoders();
}  // namespace video
