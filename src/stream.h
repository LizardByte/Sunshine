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
  constexpr auto MIC_STREAM_PORT = 13;

  // Microphone protocol constants
  constexpr uint16_t MIC_PROTOCOL_VERSION = 0x0001;
  constexpr uint16_t MIC_PACKET_AUDIO = 0x0001;
  constexpr uint16_t MIC_PACKET_CONTROL = 0x0002;
  constexpr uint16_t MIC_FLAG_ENCRYPTED = 0x0001;
  constexpr uint16_t MIC_FLAG_FEC = 0x0002;

  // Microphone packet header for client identification and multi-stream support
  struct mic_packet_header_t {
    uint16_t version;          // Protocol version (0x0001)
    uint16_t packet_type;      // 0x0001 = audio data, 0x0002 = control
    uint32_t client_id;        // Client session identifier
    uint16_t stream_id;        // Audio stream identifier (for multiple mics)
    uint16_t sequence;         // Packet sequence number
    uint32_t timestamp;        // Audio timestamp
    uint16_t payload_size;     // Size of audio payload after header
    uint16_t flags;            // Optional flags (encryption, FEC, etc.)
    // Audio payload follows
  };

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

    std::shared_ptr<session_t> alloc(config_t &config, rtsp_stream::launch_session_t &launch_session);
    int start(session_t &session, const std::string &addr_string);
    void stop(session_t &session);
    void join(session_t &session);
    state_e state(session_t &session);
  }  // namespace session
}  // namespace stream
