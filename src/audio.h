#pragma once

// local includes
#include "platform/common.h"
#include "thread_safe.h"
#include "utility.h"

namespace audio {
  enum stream_config_e : int {
    STEREO,
    HIGH_STEREO,
    SURROUND51,
    HIGH_SURROUND51,
    SURROUND71,
    HIGH_SURROUND71,
    MAX_STREAM_CONFIG
  };

  struct opus_stream_config_t {
    std::int32_t sampleRate;
    int channelCount;
    int streams;
    int coupledStreams;
    const std::uint8_t *mapping;
    int bitrate;
  };

  extern opus_stream_config_t stream_configs[MAX_STREAM_CONFIG];

  struct config_t {
    enum flags_e : int {
      HIGH_QUALITY,
      HOST_AUDIO,
      MAX_FLAGS
    };

    int packetDuration;
    int channels;
    int mask;

    std::bitset<MAX_FLAGS> flags;
  };

  struct audio_ctx_t {
    // We want to change the sink for the first stream only
    std::unique_ptr<std::atomic_bool> sink_flag;

    std::unique_ptr<platf::audio_control_t> control;

    bool restore_sink;
    platf::sink_t sink;
  };

  using buffer_t = util::buffer_t<std::uint8_t>;
  using packet_t = std::pair<void *, buffer_t>;
  using audio_ctx_ref_t = safe::shared_t<audio::audio_ctx_t>::ptr_t;

  void
  capture(safe::mail_t mail, config_t config, void *channel_data);

  /**
   * @brief Get the reference to the audio context.
   * @returns A shared pointer reference to audio context.
   * @note Aside from the configuration purposes, it can be used to extend the
   *       audio sink lifetime to capture sink earlier and restore it later.
   *
   * EXAMPLES:
   * ```cpp
   * audio_ctx_ref_t audio = get_audio_ctx_ref()
   * ```
   */
  audio_ctx_ref_t
  get_audio_ctx_ref();
}  // namespace audio
