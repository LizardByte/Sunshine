/**
 * @file src/rtsp.h
 * @brief todo
 */
#pragma once

#include <atomic>

#include "crypto.h"
#include "thread_safe.h"

namespace rtsp_stream {
  constexpr auto RTSP_SETUP_PORT = 21;

  struct launch_session_t {
    crypto::aes_t gcm_key;
    crypto::aes_t iv;

    std::string av_ping_payload;
    uint32_t control_connect_data;

    bool host_audio;
    std::string unique_id;
    int width;
    int height;
    int fps;
    int gcmap;
    int appid;
    int surround_info;
    bool enable_hdr;
    bool enable_sops;
  };

  void
  launch_session_raise(launch_session_t launch_session);
  int
  session_count();

  void
  rtpThread();

}  // namespace rtsp_stream
