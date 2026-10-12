/**
 * @file src/rtsp.h
 * @brief Declarations for RTSP streaming.
 */
#pragma once

// standard includes
#include <atomic>

// local includes
#include "crypto.h"
#include "thread_safe.h"

namespace config {
  struct settings_t;
}

namespace stream {
  class service_t;
}

namespace input {
  class service_t;
}

namespace video {
  class service_t;
}

namespace rtsp_stream {
  constexpr auto RTSP_SETUP_PORT = 21;  ///< GameStream base-port offset used for the RTSP setup listener.

  /**
   * @brief RTSP launch session state shared with stream setup.
   */
  struct launch_session_t {
    uint32_t id;  ///< RTSP launch-session identifier assigned before stream startup.

    crypto::aes_t gcm_key;  ///< AES-GCM key negotiated for encrypted RTSP messages.
    crypto::aes_t iv;  ///< Initial RTSP AES-GCM IV supplied by the client.

    std::string av_ping_payload;  ///< AV ping payload.
    uint32_t control_connect_data;  ///< Client-provided token used when connecting the control channel.

    bool host_audio;  ///< Whether host audio should be played locally.
    std::string unique_id;  ///< Moonlight client unique identifier for this launch request.
    int width;  ///< Frame or display width in pixels.
    int height;  ///< Frame or display height in pixels.
    int fps;  ///< Requested video frame rate.
    int gcmap;  ///< Game controller mapping requested by the client.
    int appid;  ///< Application ID requested for launch or resume.
    int surround_info;  ///< Encoded GameStream surround-sound capability flags.
    std::string surround_params;  ///< Client-provided surround-sound layout parameters.
    bool continuous_audio;  ///< Whether audio packets continue during silence.
    bool enable_hdr;  ///< Whether HDR streaming is requested.
    bool enable_sops;  ///< Whether sequence output protection is requested.
    std::string client_name;  ///< Friendly client name from initial pairing.

    std::optional<crypto::cipher::gcm_t> rtsp_cipher;  ///< AES-GCM cipher used once encrypted RTSP is negotiated.
    std::string rtsp_url_scheme;  ///< URL scheme selected by the RTSP SETUP flow.
    uint32_t rtsp_iv_counter;  ///< Counter value mixed into encrypted RTSP IVs.
    std::string client_cert;  ///< PEM certificate for the paired Moonlight client.
    std::function<void()> on_stream_connected;  ///< Consumes this owner's pending launch once control connects.
  };

  /**
   * @brief Own RTSP listener state and its pending and active streaming sessions.
   */
  class service_t {
  public:
    /**
     * @brief Bind the session owners without opening the listening socket.
     * @param settings Startup transport and encryption policy.
     * @param streams Stable service handle used to allocate mutable session state.
     * @param inputs Host input owner for client cleanup.
     * @param videos Read-only encoder capabilities used in RTSP negotiation.
     * @param host_mail Host mailbox for listener shutdown.
     */
    service_t(const config::settings_t &settings, const stream::service_t &streams, input::service_t &inputs, const video::service_t &videos, safe::mail_t host_mail);
    /**
     * @brief Stop and release listener state after the server loop has exited.
     */
    ~service_t();
    service_t(const service_t &) = delete;
    service_t &operator=(const service_t &) = delete;
    /**
     * @brief Queue a launch session until the RTSP client connects.
     *
     * @param launch_session Session state prepared by the GameStream launch handler.
     */
    void launch_session_raise(std::shared_ptr<launch_session_t> launch_session);

    /**
     * @brief Clear state for the specified launch session.
     * @param launch_session_id The ID of the session to clear.
     */
    void launch_session_clear(uint32_t launch_session_id);

    /**
     * @brief Get the number of active sessions.
     * @return Count of active sessions.
     */
    int session_count();

    /**
     * @brief Terminates all running streaming sessions.
     */
    void terminate_sessions();
    /**
     * @brief Terminate active sessions associated with a client certificate.
     *
     * @param cert Certificate data or object used by the operation.
     */
    void terminate_sessions_by_cert(std::string_view cert);

    /**
     * @brief Runs the RTSP server loop.
     */
    void start();

  private:
    struct impl_t;  ///< Listener and explicit dependencies.
    std::shared_ptr<impl_t> impl;  ///< Listener state owned by this RTSP service.
  };
}  // namespace rtsp_stream
