#ifndef SUNSHINE_AUDIO_H
#define SUNSHINE_AUDIO_H

#include "thread_safe.h"
#include "utility.h"
namespace audio {
enum stream_config_e : int {
  STEREO,
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

using buffer_t = util::buffer_t<std::uint8_t>;
using packet_t = std::pair<void *, buffer_t>;
void capture(safe::mail_t mail, config_t config, void *channel_data);
} // namespace audio

#endif
