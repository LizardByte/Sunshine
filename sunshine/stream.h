//
// Created by loki on 6/5/19.
//

#ifndef SUNSHINE_STREAM_H
#define SUNSHINE_STREAM_H

#include <atomic>

#include "crypto.h"
#include "thread_safe.h"

namespace stream {

enum class state_e : int {
  STOPPED,
  STOPPING,
  STARTING,
  RUNNING,
};

struct launch_session_t {
  crypto::aes_t gcm_key;
  crypto::aes_t iv;

  bool has_process;
};

extern safe::event_t<launch_session_t> launch_event;
extern std::atomic<state_e> session_state;

void rtpThread(std::shared_ptr<safe::event_t<bool>> shutdown_event);

}

#endif //SUNSHINE_STREAM_H
