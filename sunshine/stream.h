//
// Created by loki on 6/5/19.
//

#ifndef SUNSHINE_STREAM_H
#define SUNSHINE_STREAM_H

#include <boost/asio.hpp>

#include "video.h"
#include "audio.h"
#include "crypto.h"

namespace input {
struct input_t;
}

namespace stream {
struct session_t;
struct config_t {
  audio::config_t audio;
  video::config_t monitor;
  int packetsize;

  bool sops;
  std::optional<int> gcmap;
};

std::shared_ptr<session_t> alloc_session(config_t &config, crypto::aes_t &gcm_key, crypto::aes_t &iv);
void start_session(std::shared_ptr<session_t> session, const std::string &addr_string);

void stop(session_t &session);
void join(session_t &session);

extern std::shared_ptr<input::input_t> input;
}

#endif //SUNSHINE_STREAM_H
