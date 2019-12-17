//
// Created by loki on 6/5/19.
//

#ifndef SUNSHINE_STREAM_H
#define SUNSHINE_STREAM_H

#include <atomic>

#include "crypto.h"
#include "thread_safe.h"
namespace stream {

struct launch_session_t {
  crypto::aes_t gcm_key;
  crypto::aes_t iv;

  bool has_process;
};

extern safe::event_t<launch_session_t> launch_event;
extern std::atomic_bool has_session;

void rtpThread();

}

#endif //SUNSHINE_STREAM_H
