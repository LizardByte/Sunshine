/**
 * @file src/stream.h
 * @brief Declarations for the streaming protocols.
 */
#pragma once

// standard includes
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

    /**
     * @brief Information about an active streaming session.
     */
    struct session_info_t {
      std::string id;           ///< Unique session identifier
      std::string client_name;  ///< Name of the connected client
      std::string ip_address;   ///< Client's IP address
      std::chrono::steady_clock::time_point start_time;  ///< When the session started
    };

    std::shared_ptr<session_t> alloc(config_t &config, rtsp_stream::launch_session_t &launch_session);
    int start(session_t &session, const std::string &addr_string);
    void stop(session_t &session);
    void join(session_t &session);
    state_e state(session_t &session);

    /**
     * @brief Get a list of all active streaming sessions.
     * @return Vector of session information.
     */
    std::vector<session_info_t> get_all_sessions();

    /**
     * @brief Disconnect a session by its ID.
     * @param session_id The unique session identifier.
     * @return true if the session was found and stopped, false otherwise.
     */
    bool disconnect(const std::string &session_id);
  }  // namespace session
}  // namespace stream
