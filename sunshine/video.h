//
// Created by loki on 6/9/19.
//

#ifndef SUNSHINE_VIDEO_H
#define SUNSHINE_VIDEO_H

#include "queue.h"

struct AVPacket;
namespace video {
void free_packet(AVPacket *packet);

using packet_t = util::safe_ptr<AVPacket, free_packet>;

struct config_t {
  int width;
  int height;
  int framerate;
  int bitrate;
  int slicesPerFrame;
};

void capture_display(std::shared_ptr<safe::queue_t<packet_t>> packets, config_t config);
}

#endif //SUNSHINE_VIDEO_H
