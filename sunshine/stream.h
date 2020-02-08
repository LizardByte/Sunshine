//
// Created by loki on 6/5/19.
//

#ifndef SUNSHINE_STREAM_H
#define SUNSHINE_STREAM_H

#include <boost/asio.hpp>

#include "thread_safe.h"
#include "video.h"
#include "audio.h"
#include "crypto.h"

namespace input {
struct input_t;
}

namespace stream {
constexpr auto VIDEO_STREAM_PORT = 47998;
constexpr auto CONTROL_PORT = 47999;
constexpr auto AUDIO_STREAM_PORT = 48000;

namespace asio = boost::asio;
namespace sys  = boost::system;

using asio::ip::tcp;
using asio::ip::udp;

enum class socket_e : int {
  video,
  audio
};

enum class state_e : int {
  STOPPED,
  STOPPING,
  STARTING,
  RUNNING,
};

using message_queue_t = std::shared_ptr<safe::queue_t<std::pair<std::uint16_t, std::string>>>;
using message_queue_queue_t = std::shared_ptr<safe::queue_t<std::tuple<socket_e, asio::ip::address, message_queue_t>>>;

struct config_t {
  audio::config_t audio;
  video::config_t monitor;
  int packetsize;

  bool sops;
  std::optional<int> gcmap;
};

struct broadcast_ctx_t {
  safe::event_t<bool> shutdown_event;

  video::packet_queue_t video_packets;
  audio::packet_queue_t audio_packets;

  message_queue_queue_t session_queue;

  std::thread video_thread;
  std::thread audio_thread;
  std::thread control_thread;

  std::thread recv_thread;

  asio::io_service io;
  udp::socket video_sock { io, udp::endpoint(udp::v6(), VIDEO_STREAM_PORT) };
  udp::socket audio_sock { io, udp::endpoint(udp::v6(), AUDIO_STREAM_PORT) };
};

struct session_t {
  config_t config;

  std::thread audioThread;
  std::thread videoThread;

  std::chrono::steady_clock::time_point pingTimeout;

  udp::endpoint video_peer;
  udp::endpoint audio_peer;



  video::idr_event_t idr_events;

  crypto::aes_t gcm_key;
  crypto::aes_t iv;

  std::atomic<state_e> state;
};

void videoThread(std::shared_ptr<session_t> session, std::string addr_str);
void audioThread(std::shared_ptr<session_t> session, std::string addr_str);

void stop(session_t &session);

extern std::shared_ptr<input::input_t> input;
}

#endif //SUNSHINE_STREAM_H
