//
// Created by loki on 6/9/19.
//

#ifndef SUNSHINE_VIDEO_H
#define SUNSHINE_VIDEO_H

#include "thread_safe.h"

struct AVPacket;
namespace video {
void free_packet(AVPacket *packet);

using packet_t       = util::safe_ptr<AVPacket, free_packet>;
using packet_queue_t = std::shared_ptr<safe::queue_t<packet_t>>;
using idr_event_t    = std::shared_ptr<safe::event_t<std::pair<int64_t, int64_t>>>;

struct config_t {
  int width;
  int height;
  int framerate;
  int bitrate;
  int slicesPerFrame;
  int numRefFrames;
};

void capture_display(packet_queue_t packets, idr_event_t idr_events, config_t config);
}

#endif //SUNSHINE_VIDEO_H
