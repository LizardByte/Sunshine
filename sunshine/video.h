//
// Created by loki on 6/9/19.
//

#ifndef SUNSHINE_VIDEO_H
#define SUNSHINE_VIDEO_H

#include "thread_safe.h"
#include "input.h"
#include "platform/common.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVPacket;
namespace video {

struct packet_raw_t : public AVPacket {
  void init_packet() {
    pts                  = AV_NOPTS_VALUE;
    dts                  = AV_NOPTS_VALUE;
    pos                  = -1;
    duration             = 0;
    flags                = 0;
    stream_index         = 0;
    buf                  = nullptr;
    side_data            = nullptr;
    side_data_elems      = 0;
  }

  template<class P>
  explicit packet_raw_t(P *user_data) : channel_data { user_data } {
    init_packet();
  }

  explicit packet_raw_t(std::nullptr_t) : channel_data { nullptr } {
    init_packet();
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
