/**
 * @file src/rtsp.h
 * @brief todo
 */
#pragma once

#include <atomic>

#include "crypto.h"
#include "thread_safe.h"

namespace rtsp_stream {
  constexpr auto RTSP_SETUP_PORT = 21;

  struct launch_session_t {
    uint32_t id;

    crypto::aes_t gcm_key;
    crypto::aes_t iv;

    std::string av_ping_payload;
    uint32_t control_connect_data;

    bool host_audio;
    std::string unique_id;
    int width;
    int height;
    int fps;
    int gcmap;
    int appid;
    int surround_info;
    bool enable_hdr;
    bool enable_sops;

    std::optional<crypto::cipher::gcm_t> rtsp_cipher;
    std::string rtsp_url_scheme;
    uint32_t rtsp_iv_counter;
  };

  void
  launch_session_raise(std::shared_ptr<launch_session_t> launch_session);

  void
  launch_session_clear(uint32_t launch_session_id);

  int
  session_count();

  void
  rtpThread();

}  // namespace rtsp_stream
