//
// Created by loki on 6/5/19.
//

#include "process.h"

#include <queue>
#include <future>

#include <fstream>
#include <openssl/err.h>

extern "C" {
#include <moonlight-common-c/src/Video.h>
#include <rs.h>
}

#include "network.h"
#include "config.h"
#include "utility.h"
#include "stream.h"
#include "thread_safe.h"
#include "input.h"
#include "main.h"

#define IDX_START_A 0
#define IDX_REQUEST_IDR_FRAME 0
#define IDX_START_B 1
#define IDX_INVALIDATE_REF_FRAMES 2
#define IDX_LOSS_STATS 3
#define IDX_INPUT_DATA 5
#define IDX_RUMBLE_DATA 6
#define IDX_TERMINATION 7

static const short packetTypes[] = {
  0x0305, // Start A
  0x0307, // Start B
  0x0301, // Invalidate reference frames
  0x0201, // Loss Stats
  0x0204, // Frame Stats (unused)
  0x0206, // Input data
  0x010b, // Rumble data
  0x0100, // Termination
};

constexpr auto VIDEO_STREAM_PORT = 47998;
constexpr auto CONTROL_PORT = 47999;
constexpr auto AUDIO_STREAM_PORT = 48000;

namespace asio = boost::asio;
namespace sys  = boost::system;

using asio::ip::tcp;
using asio::ip::udp;

using namespace std::literals;

namespace stream {

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

#pragma pack(push, 1)

struct video_packet_raw_t {
  uint8_t *payload() {
    return (uint8_t *)(this + 1);
  }

  RTP_PACKET rtp;
  NV_VIDEO_PACKET packet;
};

struct audio_packet_raw_t {
  uint8_t *payload() {
    return (uint8_t *)(this + 1);
  }

  RTP_PACKET rtp;
};

#pragma pack(pop)

using rh_t           = util::safe_ptr<reed_solomon, reed_solomon_release>;
using video_packet_t = util::c_ptr<video_packet_raw_t>;
using audio_packet_t = util::c_ptr<audio_packet_raw_t>;

using message_queue_t = std::shared_ptr<safe::queue_t<std::pair<std::uint16_t, std::string>>>;
using message_queue_queue_t = std::shared_ptr<safe::queue_t<std::tuple<socket_e, asio::ip::address, message_queue_t>>>;
using session_queue_t = std::shared_ptr<safe::queue_t<std::pair<std::string, std::shared_ptr<session_t>>>>;

struct broadcast_ctx_t {
  safe::event_t<bool> shutdown_event;

  video::packet_queue_t video_packets;
  audio::packet_queue_t audio_packets;

  message_queue_queue_t message_queue_queue;
  session_queue_t session_queue;

  std::thread video_thread;
  std::thread audio_thread;
  std::thread control_thread;

  std::thread recv_thread;

  asio::io_service io;

  udp::socket video_sock { io, udp::endpoint(udp::v4(), VIDEO_STREAM_PORT) };
  udp::socket audio_sock { io, udp::endpoint(udp::v4(), AUDIO_STREAM_PORT) };
};

struct session_t {
  config_t config;

  std::thread audioThread;
  std::thread videoThread;

  std::chrono::steady_clock::time_point pingTimeout;

  safe::shared_t<broadcast_ctx_t>::ptr_t broadcast_ref;
  udp::endpoint video_peer;
  udp::endpoint audio_peer;

  video::idr_event_t idr_events;

  crypto::aes_t gcm_key;
  crypto::aes_t iv;

  std::atomic<state_e> state;
};

void videoThread(std::shared_ptr<session_t> session, std::string addr_str);
void audioThread(std::shared_ptr<session_t> session, std::string addr_str);

int start_broadcast(broadcast_ctx_t &ctx);
void end_broadcast(broadcast_ctx_t &ctx);

std::shared_ptr<input::input_t> input;
static auto broadcast = safe::make_shared<broadcast_ctx_t>(start_broadcast, end_broadcast);

class control_server_t {
public:
  control_server_t(control_server_t &&) noexcept = default;
  control_server_t &operator=(control_server_t &&) noexcept = default;

  explicit control_server_t(session_queue_t session_queue, std::uint16_t port) : session_queue { session_queue }, _host { net::host_create(_addr, config::stream.channels, port) } {}

  template<class T, class X>
  void iterate(std::chrono::duration<T, X> timeout) {
    ENetEvent event;
    auto res = enet_host_service(_host.get(), &event, std::chrono::floor<std::chrono::milliseconds>(timeout).count());

    if(res > 0) {
      while(session_queue->peek()) {
        auto session_opt = session_queue->pop();
        if(!session_opt) {
          return;
        }

        TUPLE_2D_REF(addr_string, session, *session_opt);

        if(session) {
          _map_addr_session.try_emplace(addr_string, session);
        }
        else {
          _map_addr_session.erase(addr_string);
        }
      }
      auto addr_string = platf::from_sockaddr((sockaddr*)&event.peer->address.address);

      auto it = _map_addr_session.find(addr_string);
      if(it == std::end(_map_addr_session)) {
        BOOST_LOG(warning) << "Rejected connection from ["sv << addr_string << "]: it's not properly set up"sv;
        enet_peer_disconnect_now(event.peer, 0);

        return;
      }

      auto &session = it->second;

      switch(event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
        {
          net::packet_t packet { event.packet };

          std::uint16_t *type = (std::uint16_t *)packet->data;
          std::string_view payload { (char*)packet->data + sizeof(*type), packet->dataLength - sizeof(*type) };


          auto cb = _map_type_cb.find(*type);
          if(cb == std::end(_map_type_cb)) {
            BOOST_LOG(warning)
              << "type [Unknown] { "sv << util::hex(*type).to_string_view() << " }"sv << std::endl
              << "---data---"sv << std::endl << util::hex_vec(payload) << std::endl << "---end data---"sv;
          }

          else {
            cb->second(session.get(), payload);
          }
        }
          break;
        case ENET_EVENT_TYPE_CONNECT:
          BOOST_LOG(info) << "CLIENT CONNECTED"sv;
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          BOOST_LOG(info) << "CLIENT DISCONNECTED"sv;
          // No more clients to send video data to ^_^
          if(session->state == state_e::RUNNING) {
            stop(*session);
          }
          break;
        case ENET_EVENT_TYPE_NONE:
          break;
      }
    }
  }

  void map(uint16_t type, std::function<void(session_t *, const std::string_view&)> cb) {
    _map_type_cb.emplace(type, std::move(cb));
  }

  void send(const std::string_view &payload);

  std::unordered_map<std::uint16_t, std::function<void(session_t *, const std::string_view&)>> _map_type_cb;
  std::unordered_map<std::string, std::shared_ptr<session_t>> _map_addr_session;

  session_queue_t session_queue;

  ENetAddress _addr;
  net::host_t _host;
};

namespace fec {
using rs_t = util::safe_ptr<reed_solomon, reed_solomon_release>;

struct fec_t {
  size_t data_shards;
  size_t nr_shards;
  size_t percentage;

  size_t blocksize;
  util::buffer_t<char> shards;

  std::string_view operator[](size_t el) const {
    return { &shards[el*blocksize], blocksize };
  }

  size_t size() const {
    return nr_shards;
  }
};

fec_t encode(const std::string_view &payload, size_t blocksize, size_t fecpercentage) {
  auto payload_size = payload.size();

  auto pad = payload_size % blocksize != 0;

  auto data_shards   = payload_size / blocksize + (pad ? 1 : 0);
  auto parity_shards = (data_shards * fecpercentage + 99) / 100;
  auto nr_shards = data_shards + parity_shards;

  if(nr_shards > DATA_SHARDS_MAX) {
    BOOST_LOG(error)
      << "Number of fragments for reed solomon exceeds DATA_SHARDS_MAX"sv << std::endl
      << nr_shards << " > "sv << DATA_SHARDS_MAX;

    return { 0 };
  }

  util::buffer_t<char> shards { nr_shards * blocksize };
  util::buffer_t<uint8_t*> shards_p { nr_shards };

  // copy payload + padding
  auto next = std::copy(std::begin(payload), std::end(payload), std::begin(shards));
  std::fill(next, std::end(shards), 0); // padding with zero

  for(auto x = 0; x < nr_shards; ++x) {
    shards_p[x] = (uint8_t*)&shards[x * blocksize];
  }

  // packets = parity_shards + data_shards
  rs_t rs { reed_solomon_new(data_shards, parity_shards) };

  reed_solomon_encode(rs.get(), shards_p.begin(), nr_shards, blocksize);

  return {
    data_shards,
    nr_shards,
    fecpercentage,
    blocksize,
    std::move(shards)
  };
}
}

template<class F>
std::vector<uint8_t> insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data, F &&f) {
  auto pad = data.size() % slice_size != 0;
  auto elements = data.size() / slice_size + (pad ? 1 : 0);

  std::vector<uint8_t> result;
  result.resize(elements * insert_size + data.size());

  auto next = std::begin(data);
  for(auto x = 0; x < elements - 1; ++x) {
    void *p = &result[x*(insert_size + slice_size)];

    f(p, x, elements);

    std::copy(next, next + slice_size, (char*)p + insert_size);
    next += slice_size;
  }

  auto x = elements - 1;
  void *p = &result[x*(insert_size + slice_size)];

  f(p, x, elements);

  std::copy(next, std::end(data), (char*)p + insert_size);

  return result;
}

std::vector<uint8_t> replace(const std::string_view &original, const std::string_view &old, const std::string_view &_new) {
  std::vector<uint8_t> replaced;

  auto begin = std::begin(original);
  auto next = std::search(begin, std::end(original), std::begin(old), std::end(old));

  std::copy(begin, next, std::back_inserter(replaced));
  std::copy(std::begin(_new), std::end(_new), std::back_inserter(replaced));
  std::copy(next + old.size(), std::end(original), std::back_inserter(replaced));

  return replaced;
}

void control_server_t::send(const std::string_view & payload) {
  std::for_each(_host->peers, _host->peers + _host->peerCount, [payload](auto &peer) {
    auto packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
    if(enet_peer_send(&peer, 0, packet)) {
      enet_packet_destroy(packet);
    }
  });

  enet_host_flush(_host.get());
}

void controlBroadcastThread(safe::event_t<bool> *shutdown_event, session_queue_t session_queue) {
  control_server_t server { session_queue, CONTROL_PORT };

  server.map(packetTypes[IDX_START_A], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_START_A]"sv;
  });

  server.map(packetTypes[IDX_START_B], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_START_B]"sv;
  });

  server.map(packetTypes[IDX_LOSS_STATS], [&](session_t *session, const std::string_view &payload) {
    int32_t *stats = (int32_t*)payload.data();
    auto count = stats[0];
    std::chrono::milliseconds t { stats[1] };

    auto lastGoodFrame = stats[3];

    BOOST_LOG(verbose)
      << "type [IDX_LOSS_STATS]"sv << std::endl
      << "---begin stats---" << std::endl
      << "loss count since last report [" << count << ']' << std::endl
      << "time in milli since last report [" << t.count() << ']' << std::endl
      << "last good frame [" << lastGoodFrame << ']' << std::endl
      << "---end stats---";
  });

  server.map(packetTypes[IDX_INVALIDATE_REF_FRAMES], [&](session_t *session, const std::string_view &payload) {
    std::int64_t *frames = (std::int64_t *)payload.data();
    auto firstFrame = frames[0];
    auto lastFrame = frames[1];

    BOOST_LOG(debug)
      << "type [IDX_INVALIDATE_REF_FRAMES]"sv << std::endl
      << "firstFrame [" << firstFrame << ']' << std::endl
      << "lastFrame [" << lastFrame << ']';

    session->idr_events->raise(std::make_pair(firstFrame, lastFrame));
  });

  server.map(packetTypes[IDX_INPUT_DATA], [&](session_t *session, const std::string_view &payload) {
    session->pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    BOOST_LOG(debug) << "type [IDX_INPUT_DATA]"sv;

    int32_t tagged_cipher_length = util::endian::big(*(int32_t*)payload.data());
    std::string_view tagged_cipher { payload.data() + sizeof(tagged_cipher_length), (size_t)tagged_cipher_length };

    crypto::cipher_t cipher { session->gcm_key };
    cipher.padding = false;

    std::vector<uint8_t> plaintext;
    if(cipher.decrypt_gcm(session->iv, tagged_cipher, plaintext)) {
      // something went wrong :(

      BOOST_LOG(error) << "Failed to verify tag"sv;

      stop(*session);
    }

    if(tagged_cipher_length >= 16 + session->iv.size()) {
      std::copy(payload.end() - 16, payload.end(), std::begin(session->iv));
    }

    input::print(plaintext.data());
    input::passthrough(input, std::move(plaintext));
  });

  while(!shutdown_event->peek()) {
    auto now = std::chrono::steady_clock::now();
    for(auto &[addr,session] : server._map_addr_session) {
      if(now > session->pingTimeout) {
        BOOST_LOG(info) << addr << ": Ping Timeout"sv;
        stop(*session);
      }
    }

    if(proc::proc.running() == -1) {
      BOOST_LOG(debug) << "Process terminated"sv;

      std::uint16_t reason = 0x0100;

      std::array<std::uint16_t, 2> payload;
      payload[0] = packetTypes[IDX_TERMINATION];
      payload[1] = reason;

      server.send(std::string_view {(char*)payload.data(), payload.size()});

      //TODO: Terminate session
    }

    server.iterate(500ms);
  }
}

void recvThread(broadcast_ctx_t &ctx) {
  std::map<asio::ip::address, message_queue_t> peer_to_video_session;
  std::map<asio::ip::address, message_queue_t> peer_to_audio_session;

  auto &video_sock = ctx.video_sock;
  auto &audio_sock = ctx.audio_sock;

  auto &message_queue_queue = ctx.message_queue_queue;
  auto &io = ctx.io;

  udp::endpoint peer;

  std::array<char, 2048> buf[2];
  std::function<void(const boost::system::error_code, size_t)> recv_func[2];

  auto populate_peer_to_session = [&]() {
    while(message_queue_queue->peek()) {
      auto message_queue_opt = message_queue_queue->pop();
      TUPLE_3D_REF(socket_type, addr, message_queue, *message_queue_opt);

      switch(socket_type) {
        case socket_e::video:
          if(message_queue) {
            peer_to_video_session.emplace(addr, message_queue);
          }
          else {
            peer_to_video_session.erase(addr);
          }
          break;
        case socket_e::audio:
          if(message_queue) {
            peer_to_audio_session.emplace(addr, message_queue);
          }
          else {
            peer_to_audio_session.erase(addr);
          }
          break;
      }
    }
  };

  auto recv_func_init = [&](udp::socket &sock, int buf_elem, std::map<asio::ip::address, message_queue_t> &peer_to_session) {
    recv_func[buf_elem] = [&,buf_elem](const boost::system::error_code &ec, size_t bytes) {
      auto type_str = buf_elem ? "AUDIO"sv : "VIDEO"sv;

      populate_peer_to_session();

      if(ec || !bytes) {
        BOOST_LOG(fatal) << "Couldn't receive data from udp socket: "sv << ec.message();

        log_flush();
        std::abort();
      }

      auto it = peer_to_session.find(peer.address());
      if(it != std::end(peer_to_session)) {
        BOOST_LOG(debug) << "RAISE: "sv << peer.address().to_string() << ":"sv << peer.port() << " :: " << type_str;
        it->second->raise(peer.port(), std::string { buf[buf_elem].data(), bytes });
      }

      sock.async_receive_from(asio::buffer(buf[buf_elem]), peer, 0, recv_func[buf_elem]);
    };
  };

  recv_func_init(video_sock, 0, peer_to_video_session);
  recv_func_init(audio_sock, 1, peer_to_audio_session);

  video_sock.async_receive_from(asio::buffer(buf[0]), peer, 0, recv_func[0]);
  audio_sock.async_receive_from(asio::buffer(buf[1]), peer, 0, recv_func[1]);

  while(!ctx.shutdown_event.peek()) {
    io.run();
  }
}

void videoBroadcastThread(safe::event_t<bool> *shutdown_event, udp::socket &sock, video::packet_queue_t packets) {
  int lowseq = 0;
  while(auto packet = packets->pop()) {
    if(shutdown_event->peek()) {
      break;
    }

    auto session = (session_t*)packet->channel_data;

    std::string_view payload{(char *) packet->data, (size_t) packet->size};
    std::vector<uint8_t> payload_new;

    auto nv_packet_header = "\0017charss"sv;
    std::copy(std::begin(nv_packet_header), std::end(nv_packet_header), std::back_inserter(payload_new));
    std::copy(std::begin(payload), std::end(payload), std::back_inserter(payload_new));

    payload = {(char *) payload_new.data(), payload_new.size()};

    // make sure moonlight recognizes the nalu code for IDR frames
    if (packet->flags & AV_PKT_FLAG_KEY) {
      // TODO: Not all encoders encode their IDR frames with the 4 byte NALU prefix
      std::string_view frame_old = "\000\000\001e"sv;
      std::string_view frame_new = "\000\000\000\001e"sv;
      if(session->config.monitor.videoFormat != 0) {
        frame_old = "\000\000\001("sv;
        frame_new = "\000\000\000\001("sv;
      }

      assert(std::search(std::begin(payload), std::end(payload), std::begin(hevc_i_frame), std::end(hevc_i_frame)) ==
             std::end(payload));
      payload_new = replace(payload, frame_old, frame_new);
      payload = {(char *) payload_new.data(), payload_new.size()};
    }

    // insert packet headers
    auto blocksize = session->config.packetsize + MAX_RTP_HEADER_SIZE;
    auto payload_blocksize = blocksize - sizeof(video_packet_raw_t);

    auto fecPercentage = config::stream.fec_percentage;

    payload_new = insert(sizeof(video_packet_raw_t), payload_blocksize,
                         payload, [&](void *p, int fecIndex, int end) {
        video_packet_raw_t *video_packet = (video_packet_raw_t *)p;

        video_packet->packet.flags = FLAG_CONTAINS_PIC_DATA;
        video_packet->packet.frameIndex = packet->pts;
        video_packet->packet.streamPacketIndex = ((uint32_t)lowseq + fecIndex) << 8;
        video_packet->packet.fecInfo = (
          fecIndex << 12 |
          end << 22 |
          fecPercentage << 4
        );

        if(fecIndex == 0) {
          video_packet->packet.flags |= FLAG_SOF;
        }

        if(fecIndex == end - 1) {
          video_packet->packet.flags |= FLAG_EOF;
        }

        video_packet->rtp.sequenceNumber = util::endian::big<uint16_t>(lowseq + fecIndex);
      });

    payload = {(char *) payload_new.data(), payload_new.size()};

    auto shards = fec::encode(payload, blocksize, fecPercentage);
    if(shards.data_shards == 0) {
      BOOST_LOG(info) << "skipping frame..."sv << std::endl;
      continue;
    }

    for (auto x = shards.data_shards; x < shards.size(); ++x) {
      video_packet_raw_t *inspect = (video_packet_raw_t *)shards[x].data();

      inspect->packet.frameIndex = packet->pts;
      inspect->packet.fecInfo = (
        x << 12 |
        shards.data_shards << 22 |
        fecPercentage << 4
      );

      inspect->rtp.sequenceNumber = util::endian::big<uint16_t>(lowseq + x);
    }

    for (auto x = 0; x < shards.size(); ++x) {
      sock.send_to(asio::buffer(shards[x]), session->video_peer);
    }

    if(packet->flags & AV_PKT_FLAG_KEY) {
      BOOST_LOG(verbose) << "Key Frame ["sv << packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv;
    }
    else {
      BOOST_LOG(verbose) << "Frame ["sv << packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv << std::endl;
    }

    lowseq += shards.size();
  }

  shutdown_event->raise(true);
}

void audioBroadcastThread(safe::event_t<bool> *shutdown_event, udp::socket &sock, audio::packet_queue_t packets) {
  uint16_t frame{1};

  while (auto packet = packets->pop()) {
    if(shutdown_event->peek()) {
      break;
    }

    TUPLE_2D_REF(session, packet_data, *packet);
    audio_packet_t audio_packet { (audio_packet_raw_t*)malloc(sizeof(audio_packet_raw_t) + packet_data.size()) };

    audio_packet->rtp.header = 0;
    audio_packet->rtp.packetType = 97;
    audio_packet->rtp.sequenceNumber = util::endian::big(frame++);
    audio_packet->rtp.timestamp = 0;
    audio_packet->rtp.ssrc = 0;

    std::copy(std::begin(packet_data), std::end(packet_data), audio_packet->payload());

    sock.send_to(asio::buffer((char*)audio_packet.get(), sizeof(audio_packet_raw_t) + packet_data.size()), ((session_t*)session)->audio_peer);
    BOOST_LOG(verbose) << "Audio ["sv << frame - 1 << "] ::  send..."sv;
  }

  shutdown_event->raise(true);
}

int start_broadcast(broadcast_ctx_t &ctx) {
  ctx.video_packets = std::make_shared<video::packet_queue_t::element_type>();
  ctx.audio_packets = std::make_shared<audio::packet_queue_t::element_type>();
  ctx.message_queue_queue = std::make_shared<message_queue_queue_t::element_type>();
  ctx.session_queue = std::make_shared<session_queue_t::element_type>();

  ctx.video_thread = std::thread { videoBroadcastThread, &ctx.shutdown_event, std::ref(ctx.video_sock), ctx.video_packets };
  ctx.audio_thread = std::thread { audioBroadcastThread, &ctx.shutdown_event, std::ref(ctx.audio_sock), ctx.audio_packets };
  ctx.control_thread = std::thread { controlBroadcastThread, &ctx.shutdown_event, ctx.session_queue };

  ctx.recv_thread = std::thread { recvThread, std::ref(ctx) };

  return 0;
}

void end_broadcast(broadcast_ctx_t &ctx) {
  ctx.shutdown_event.raise(true);
  ctx.video_packets->stop();
  ctx.audio_packets->stop();
  ctx.message_queue_queue->stop();
  ctx.io.stop();

  ctx.video_sock.cancel();
  ctx.audio_sock.cancel();

  BOOST_LOG(debug) << "Waiting for video thread to end..."sv;
  ctx.video_thread.join();
  BOOST_LOG(debug) << "Waiting for audio thread to end..."sv;
  ctx.audio_thread.join();
  BOOST_LOG(debug) << "Waiting for control thread to end..."sv;
  ctx.control_thread.join();
  BOOST_LOG(debug) << "All broadcasting threads ended"sv;

  ctx.video_packets.reset();
  ctx.audio_packets.reset();
}

int recv_ping(decltype(broadcast)::ptr_t ref, socket_e type, asio::ip::address &addr, std::chrono::milliseconds timeout) {
  constexpr char ping[] = {
    0x50, 0x49, 0x4E, 0x47
  };

  auto messages = std::make_shared<message_queue_t::element_type>();
  ref->message_queue_queue->raise(type, addr, messages);

  auto fg = util::fail_guard([&]() {
    // remove message queue from session
    ref->message_queue_queue->raise(type, addr, nullptr);
  });

  auto msg_opt = messages->pop(config::stream.ping_timeout);
  messages->stop();

  if(!msg_opt) {
    BOOST_LOG(error) << "Ping Timeout"sv;

    return -1;
  }

  TUPLE_2D_REF(port, msg, *msg_opt);
  if(msg != ping) {
    BOOST_LOG(error) << "First message is not a PING"sv;

    return -1;
  }

  return port;
}

void videoThread(std::shared_ptr<session_t> session, std::string addr_str) {
  auto fg = util::fail_guard([&]() {
    stop(*session);
  });

  while(session->state == state_e::STARTING) {
    std::this_thread::sleep_for(1ms);
  }

  auto addr = asio::ip::make_address(addr_str);

  auto ref = broadcast.ref();
  auto port = recv_ping(ref, socket_e::video, addr, config::stream.ping_timeout);
  if(port < 0) {
    return;
  }

  session->video_peer.address(addr);
  session->video_peer.port(port);

  BOOST_LOG(debug) << "Start capturing Video"sv;
  video::capture(ref->video_packets, session->idr_events, session->config.monitor, session.get());
}

void audioThread(std::shared_ptr<session_t> session, std::string addr_str) {
  auto fg = util::fail_guard([&]() {
    stop(*session);
  });

  while(session->state == state_e::STARTING) {
    std::this_thread::sleep_for(1ms);
  }

  auto addr = asio::ip::make_address(addr_str);

  auto ref = broadcast.ref();
  auto port = recv_ping(ref, socket_e::audio, addr, config::stream.ping_timeout);
  if(port < 0) {
    return;
  }

  session->audio_peer.address(addr);
  session->audio_peer.port(port);

  BOOST_LOG(debug) << "Start capturing Audio"sv;
  audio::capture(ref->audio_packets, session->config.audio, session.get());
}

void stop(session_t &session) {
  session.idr_events->stop();

  auto expected = state_e::RUNNING;
  session.state.compare_exchange_strong(expected, state_e::STOPPING);
}

void join(session_t &session) {
  BOOST_LOG(debug) << "Waiting for video to end..."sv;
  session.videoThread.join();
  BOOST_LOG(debug) << "Waiting for audio to end..."sv;
  session.audioThread.join();
}

void start_session(std::shared_ptr<session_t> session, const std::string &addr_string) {
  session->broadcast_ref = broadcast.ref();
  session->broadcast_ref->session_queue->raise(addr_string, session);

  session->pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

  session->audioThread = std::thread {audioThread, session, addr_string};
  session->videoThread = std::thread {videoThread, session, addr_string};

  session->state.store(state_e::RUNNING, std::memory_order_relaxed);
}

std::shared_ptr<session_t> alloc_session(config_t &config, crypto::aes_t &gcm_key, crypto::aes_t &iv) {
  auto session = std::make_shared<session_t>();

  session->config = config;
  session->gcm_key = gcm_key;
  session->iv = iv;

  session->idr_events = std::make_shared<video::idr_event_t::element_type>();
  session->state.store(state_e::STOPPED, std::memory_order_relaxed);

  return session;
}
}
