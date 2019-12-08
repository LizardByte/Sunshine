#ifndef SUNSHINE_AUDIO_H
#define SUNSHINE_AUDIO_H

#include "utility.h"
#include "queue.h"
namespace audio {
struct config_t {
  int packetDuration;
  int channels;
  int mask;
};

using packet_t = util::buffer_t<std::uint8_t>;
using packet_queue_t = std::shared_ptr<safe::queue_t<packet_t>>;
void capture(std::shared_ptr<safe::queue_t<packet_t>> packets, config_t config);
}

#endif
