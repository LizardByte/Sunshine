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

    bool host_audio;
  };

  void
  launch_session_raise(launch_session_t launch_session);
  int
  session_count();

  void
  rtpThread();

}  // namespace rtsp_stream
