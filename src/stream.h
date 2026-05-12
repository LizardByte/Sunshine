/**
 * @file src/stream.h
 * @brief Declarations for the streaming protocols.
 */
#pragma once

// standard includes
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

// lib includes
#include <boost/asio.hpp>

// local includes
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
    int mlFeatureFlags;
    int controlProtocolType;
    int audioQosType;
    int videoQosType;

    uint32_t encryptionFlagsEnabled;

    std::optional<int> gcmap;
  };

  namespace session {
    enum class state_e : int {
      STOPPED,  ///< The session is stopped
      STOPPING,  ///< The session is stopping
      STARTING,  ///< The session is starting
      RUNNING,  ///< The session is running
    };

    struct diag_snapshot_t {
      state_e state;
      bool audio_ping_ready;
      bool video_ping_ready;
      bool control_peer_ready;
      bool video_started_or_promoted;
      bool audio_peer_video_promoted;
      bool rtsp_client_port_video_promoted;
      std::uint64_t video_udp_sent;
      std::uint64_t audio_udp_received;
      std::uint64_t control_encrypted_packets_received;
    };

    std::shared_ptr<session_t> alloc(config_t &config, rtsp_stream::launch_session_t &launch_session);
    int start(session_t &session, const std::string &addr_string);
    void stop(session_t &session);
    void join(session_t &session);
    state_e state(session_t &session);
    diag_snapshot_t diag_snapshot(session_t &session);
    void diag_force_announce_success_hold(session_t &session);
  }  // namespace session
}  // namespace stream
