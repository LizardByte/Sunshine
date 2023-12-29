/**
 * @file src/stream.h
 * @brief todo
 */
#pragma once

#include <boost/asio.hpp>

#include "audio.h"
#include "crypto.h"
#include "video.h"

namespace stream {
  constexpr auto VIDEO_STREAM_PORT = 9;
  constexpr auto CONTROL_PORT = 10;
  constexpr auto AUDIO_STREAM_PORT = 11;

  struct session_t;
  struct config_t {
    audio::config_t audio;
    video::config_t monitor;

    int packetsize;
    int minRequiredFecPackets;
    int nvFeatureFlags;
    int mlFeatureFlags;
    int controlProtocolType;
    int audioQosType;
    int videoQosType;

    std::optional<int> gcmap;
  };

  namespace session {
    enum class state_e : int {
      STOPPED,
      STOPPING,
      STARTING,
      RUNNING,
    };

    std::shared_ptr<session_t>
    alloc(config_t &config, crypto::aes_t &gcm_key, crypto::aes_t &iv, std::string_view av_ping_payload, uint32_t control_connect_data);
    int
    start(session_t &session, const std::string &addr_string);
    void
    stop(session_t &session);
    void
    join(session_t &session);
    state_e
    state(session_t &session);
  }  // namespace session
}  // namespace stream
