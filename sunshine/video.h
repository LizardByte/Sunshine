//
// Created by loki on 6/9/19.
//

#ifndef SUNSHINE_VIDEO_H
#define SUNSHINE_VIDEO_H

#include "thread_safe.h"
#include "platform/common.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVPacket;
namespace video {
void free_packet(AVPacket *packet);

struct packet_raw_t : public AVPacket {
  template<class P>
  explicit packet_raw_t(P *user_data) : channel_data { user_data } {
    av_init_packet(this);
  }

  explicit packet_raw_t(std::nullptr_t null) : channel_data { nullptr } {
    av_init_packet(this);
  }

  ~packet_raw_t() {
    av_packet_unref(this);
  }

  void *channel_data;
};

using packet_t       = std::unique_ptr<packet_raw_t>;
using packet_queue_t = std::shared_ptr<safe::queue_t<packet_t>>;
using idr_event_t    = std::shared_ptr<safe::event_t<std::pair<int64_t, int64_t>>>;
using img_event_t    = std::shared_ptr<safe::event_t<std::shared_ptr<platf::img_t>>>;

struct config_t {
  int width;
  int height;
  int framerate;
  int bitrate;
  int slicesPerFrame;
  int numRefFrames;
  int encoderCscMode;
  int videoFormat;
  int dynamicRange;
};

void capture(
  safe::signal_t *shutdown_event,
  packet_queue_t packets,
  idr_event_t idr_events,
  config_t config,
  void *channel_data);

int init();
}

#endif //SUNSHINE_VIDEO_H
