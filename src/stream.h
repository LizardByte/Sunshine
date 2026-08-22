/**
 * @file src/stream.h
 * @brief Declarations for the streaming protocols.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#ifdef SUNSHINE_TESTS
  #include <string_view>
#endif

// lib includes
#include <boost/asio.hpp>

// local includes
#include "audio.h"
#include "crypto.h"
#include "video.h"
#ifdef SUNSHINE_TESTS
  #include "network_metrics.h"
#endif

namespace stream {
  constexpr auto VIDEO_STREAM_PORT = 9;  ///< GameStream base-port offset used for the video UDP stream.
  constexpr auto CONTROL_PORT = 10;  ///< GameStream base-port offset used for the control channel.
  constexpr auto AUDIO_STREAM_PORT = 11;  ///< GameStream base-port offset used for the audio UDP stream.

  struct session_t;

  /**
   * @brief Stream configuration shared by capture and network senders.
   */
  struct config_t {
    audio::config_t audio;  ///< Audio capture configuration for the stream.
    video::config_t monitor;  ///< Video capture and encoder configuration for the selected monitor.

    int packetsize;  ///< Maximum payload size for network packets.
    int minRequiredFecPackets;  ///< Minimum recovery packets required before FEC is emitted.
    int mlFeatureFlags;  ///< Moonlight feature flags advertised by the client for this session.
    int controlProtocolType;  ///< GameStream control protocol variant selected by the client.
    int audioQosType;  ///< Audio QoS type.
    int videoQosType;  ///< Video QoS type.

    uint32_t encryptionFlagsEnabled;  ///< Bitmask of GameStream encryption features enabled for the session.

    std::optional<int> gcmap;  ///< Optional game-controller mapping override from the launch request.
  };

  namespace session {
    /**
     * @brief Enumerates supported state options.
     */
    enum class state_e : int {
      STOPPED,  ///< The session is stopped
      STOPPING,  ///< The session is stopping
      STARTING,  ///< The session is starting
      RUNNING,  ///< The session is running
    };

    /**
     * @brief Allocate and initialize platform input state for a stream.
     *
     * @param config Configuration values to apply.
     * @param launch_session Launch session.
     * @return Allocated object or identifier, or an error value on failure.
     */
    std::shared_ptr<session_t> alloc(config_t &config, rtsp_stream::launch_session_t &launch_session);
    /**
     * @brief Start a streaming session for the supplied peer address.
     *
     * @param session Active streaming or pairing session for the request.
     * @param addr_string Addr string.
     * @return Start status.
     */
    int start(session_t &session, const std::string &addr_string);
    /**
     * @brief Stop a streaming session and prevent more packets from being queued.
     *
     * @param session Active streaming or pairing session for the request.
     */
    void stop(session_t &session);
    /**
     * @brief Wait for worker threads owned by the session to exit.
     *
     * @param session Active streaming or pairing session for the request.
     */
    void join(session_t &session);
    /**
     * @brief Platform handle returned from stream setup.
     *
     * @param session Active streaming or pairing session for the request.
     * @return Current lifecycle state for the stream session.
     */
    state_e state(session_t &session);
    /**
     * @brief Return the paired client certificate for a stream session.
     *
     * @param session Active streaming or pairing session for the request.
     * @return PEM certificate associated with the session's client.
     */
    const std::string &client_cert(session_t &session);

#ifdef SUNSHINE_TESTS
    namespace testing {
      /**
       * @brief Return the runtime video bitrate event attached to a test session.
       *
       * @param session Streaming session allocated by the test.
       * @return Latest-wins bitrate request event used by the encoder thread.
       */
      safe::mail_raw_t::event_t<video::bitrate_reconfigure_request_t> video_bitrate_requests(session_t &session);

      /**
       * @brief Return the video bitrate copied into a test session after host ceiling enforcement.
       *
       * @param session Streaming session allocated by the test.
       * @return Effective video bitrate in kilobits per second.
       */
      std::uint32_t configured_video_bitrate(session_t &session);

      /**
       * @brief Ingest one FEC report through the control-thread orchestration path.
       *
       * @param session Streaming session allocated by the test.
       * @param payload Raw SS_FRAME_FEC_STATUS payload.
       * @param now Monotonic arrival time.
       * @param rtt_ms Test ENet round-trip time used if an elapsed window is published first.
       * @param rtt_variance_ms Test ENet round-trip-time variance.
       * @return Tracker disposition for the supplied report.
       */
      network_metrics::ingest_result_e ingest_frame_fec_status(
        session_t &session,
        std::string_view payload,
        network_metrics::time_point_t now,
        std::uint32_t rtt_ms,
        std::uint32_t rtt_variance_ms
      );

      /**
       * @brief Record one IDR or reference-frame invalidation request through production orchestration.
       *
       * @param session Streaming session allocated by the test.
       * @param now Monotonic request arrival time.
       * @param rtt_ms Test ENet round-trip time.
       * @param rtt_variance_ms Test ENet round-trip-time variance.
       */
      void record_frame_loss_request(
        session_t &session,
        network_metrics::time_point_t now,
        std::uint32_t rtt_ms,
        std::uint32_t rtt_variance_ms
      );

      /**
       * @brief Publish an elapsed telemetry window through the production observation path.
       *
       * @param session Streaming session allocated by the test.
       * @param now Monotonic publication time.
       * @param rtt_ms Test ENet round-trip time.
       * @param rtt_variance_ms Test ENet round-trip-time variance.
       * @return `true` when a window was published and observed by the controller.
       */
      bool publish_network_metrics(
        session_t &session,
        network_metrics::time_point_t now,
        std::uint32_t rtt_ms,
        std::uint32_t rtt_variance_ms
      );

      /**
       * @brief Run pending-result acknowledgement and decision dispatch as the control thread would.
       *
       * @param session Streaming session allocated by the test.
       * @param now Monotonic control-loop time.
       * @param rtt_ms Test ENet round-trip time.
       * @param rtt_variance_ms Test ENet round-trip-time variance.
       * @param control_liveness_token Test token that changes with a fresh control-channel RTT sample.
       */
      void process_adaptive_bitrate(
        session_t &session,
        network_metrics::time_point_t now,
        std::uint32_t rtt_ms,
        std::uint32_t rtt_variance_ms,
        std::uint32_t control_liveness_token
      );

      /**
       * @brief Publish one encoder result into the control-thread acknowledgement mailbox.
       *
       * @param session Streaming session allocated by the test.
       * @param result Encoder result to acknowledge on the next orchestration pass.
       */
      void publish_video_bitrate_result(session_t &session, video::bitrate_reconfigure_result_t result);

      /**
       * @brief Return whether the adaptive controller consumed valid FEC telemetry.
       *
       * @param session Streaming session allocated by the test.
       * @return `true` after at least one valid report was published.
       */
      bool adaptive_bitrate_telemetry_seen(session_t &session);

      /**
       * @brief Return whether the adaptive controller entered fixed fallback.
       *
       * @param session Streaming session allocated by the test.
       * @return `true` when adaptation stopped after an invalid encoder result or telemetry window.
       */
      bool adaptive_bitrate_in_fallback(session_t &session);
    }  // namespace testing
#endif
  }  // namespace session
}  // namespace stream
