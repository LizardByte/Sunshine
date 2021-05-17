#ifndef SUNSHINE_AUDIO_H
#define SUNSHINE_AUDIO_H

#include "thread_safe.h"
#include "utility.h"
namespace audio {
struct config_t {
  int packetDuration;
  int channels;
  int mask;
};

using packet_t       = util::buffer_t<std::uint8_t>;
using packet_queue_t = std::shared_ptr<safe::queue_t<std::pair<void *, packet_t>>>;
void capture(safe::signal_t *shutdown_event, packet_queue_t packets, config_t config, void *channel_data);
} // namespace audio

#endif
