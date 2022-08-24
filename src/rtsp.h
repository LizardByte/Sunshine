//
// Created by loki on 2/2/20.
//

#ifndef SUNSHINE_RTSP_H
#define SUNSHINE_RTSP_H

#include <atomic>

#include "crypto.h"
#include "thread_safe.h"

namespace stream {
constexpr auto RTSP_SETUP_PORT = 21;

struct launch_session_t {
  crypto::aes_t gcm_key;
  crypto::aes_t iv;

  bool host_audio;
};

void launch_session_raise(launch_session_t launch_session);
int session_count();

void rtpThread();

} // namespace stream

#endif //SUNSHINE_RTSP_H
