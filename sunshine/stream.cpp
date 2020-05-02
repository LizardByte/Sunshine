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
#include "sync.h"
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

#pragma pack(push, 1)

struct video_packet_raw_t {
  uint8_t *payload() {
    return (uint8_t *)(this + 1);
  }

  RTP_PACKET rtp;
  char reserved[4];
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

static inline void while_starting_do_nothing(std::atomic<session::state_e> &state) {
  while(state.load(std::memory_order_acquire) == session::state_e::STARTING) {
    std::this_thread::sleep_for(1ms);
  }
}

class control_server_t {
public:
  int bind(std::uint16_t port) {
    _host = net::host_create(_addr, config::stream.channels, port);

    return !(bool)_host;
  }

  void emplace_addr_to_session(const std::string &addr, session_t &session) {
    auto lg = _map_addr_session.lock();

    _map_addr_session->emplace(addr, std::make_pair(0u, &session));
  }

  // Get session associated with address.
  // If none are found, try to find a session not yet claimed. (It will be marked by a port of value 0
  // If none of those are found, return nullptr
  session_t *get_session(const net::peer_t peer);

  // Circular dependency:
  //   iterate refers to session
  //   session refers to broadcast_ctx_t
  //   broadcast_ctx_t refers to control_server_t
  // Therefore, iterate is implemented further down the source file
  void iterate(std::chrono::milliseconds timeout);

  void map(uint16_t type, std::function<void(session_t *, const std::string_view&)> cb) {
    _map_type_cb.emplace(type, std::move(cb));
  }

  void send(const std::string_view &payload) {
    std::for_each(_host->peers, _host->peers + _host->peerCount, [payload](auto &peer) {
      auto packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
      if(enet_peer_send(&peer, 0, packet)) {
        enet_packet_destroy(packet);
      }
    });

    enet_host_flush(_host.get());
  }

  // Callbacks
  std::unordered_map<std::uint16_t, std::function<void(session_t *, const std::string_view&)>> _map_type_cb;

  // Mapping ip:port to session
  util::sync_t<std::unordered_multimap<std::string, std::pair<std::uint16_t, session_t*>>> _map_addr_session;

  ENetAddress _addr;
  net::host_t _host;
};

struct broadcast_ctx_t {
  video::packet_queue_t video_packets;
  audio::packet_queue_t audio_packets;

  message_queue_queue_t message_queue_queue;

  std::thread recv_thread;
  std::thread video_thread;
  std::thread audio_thread;
  std::thread control_thread;

  asio::io_service io;

  udp::socket video_sock { io };
  udp::socket audio_sock { io };
  control_server_t control_server;
};

struct session_t {
  config_t config;
  std::shared_ptr<input::input_t> input;

  std::thread audioThread;
  std::thread videoThread;

  std::chrono::steady_clock::time_point pingTimeout;

  safe::shared_t<broadcast_ctx_t>::ptr_t broadcast_ref;

  struct {
    int lowseq;
    udp::endpoint peer;
    video::idr_event_t idr_events;
  } video;

  struct {
    std::uint16_t frame;
    udp::endpoint peer;
  } audio;

  struct {
    net::peer_t peer;
  } control;

  crypto::aes_t gcm_key;
  crypto::aes_t iv;

  safe::signal_t shutdown_event;
  safe::signal_t controlEnd;

  std::atomic<session::state_e> state;
};

int start_broadcast(broadcast_ctx_t &ctx);
void end_broadcast(broadcast_ctx_t &ctx);


static auto broadcast = safe::make_shared<broadcast_ctx_t>(start_broadcast, end_broadcast);
safe::signal_t broadcast_shutdown_event;

session_t *control_server_t::get_session(const net::peer_t peer) {
  TUPLE_2D(port, addr_string, platf::from_sockaddr_ex((sockaddr*)&peer->address.address));

  auto lg = _map_addr_session.lock();
  TUPLE_2D(begin, end, _map_addr_session->equal_range(addr_string));

  auto it = std::end(_map_addr_session.raw);
  for(auto pos = begin; pos != end; ++pos) {
    TUPLE_2D_REF(session_port, session_p, pos->second);

    if(port == session_port) {
      return session_p;
    }
    else if(session_port == 0) {
      it = pos;
    }
  }

  if(it != std::end(_map_addr_session.raw)) {
    TUPLE_2D_REF(session_port, session_p, it->second);

    session_p->control.peer = peer;
    session_port = port;

    return session_p;
  }

  return nullptr;
}

void control_server_t::iterate(std::chrono::milliseconds timeout) {
  ENetEvent event;
  auto res = enet_host_service(_host.get(), &event, timeout.count());

  if(res > 0) {
    auto session = get_session(event.peer);
    if(!session) {
      BOOST_LOG(warning) << "Rejected connection from ["sv << platf::from_sockaddr((sockaddr*)&event.peer->address.address) << "]: it's not properly set up"sv;
      enet_peer_disconnect_now(event.peer, 0);

      return;
    }

    session->pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    switch(event.type) {
      case ENET_EVENT_TYPE_RECEIVE:
      {
        net::packet_t packet { event.packet };

        auto type = (std::uint16_t *)packet->data;
        std::string_view payload { (char*)packet->data + sizeof(*type), packet->dataLength - sizeof(*type) };

        auto cb = _map_type_cb.find(*type);
        if(cb == std::end(_map_type_cb)) {
          BOOST_LOG(warning)
            << "type [Unknown] { "sv << util::hex(*type).to_string_view() << " }"sv << std::endl
            << "---data---"sv << std::endl << util::hex_vec(payload) << std::endl << "---end data---"sv;
        }

        else {
          cb->second(session, payload);
        }
      }
        break;
      case ENET_EVENT_TYPE_CONNECT:
        BOOST_LOG(info) << "CLIENT CONNECTED"sv;
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        BOOST_LOG(info) << "CLIENT DISCONNECTED"sv;
        // No more clients to send video data to ^_^
        if(session->state == session::state_e::RUNNING) {
          session::stop(*session);
        }
        break;
      case ENET_EVENT_TYPE_NONE:
        break;
    }
  }
}

namespace fec {
using rs_t = util::safe_ptr<reed_solomon, reed_solomon_release>;

struct fec_t {
  size_t data_shards;
  size_t nr_shards;
  size_t percentage;

  size_t blocksize;
  util::buffer_t<char> shards;

  char *data(size_t el) {
    return &shards[el*blocksize];
  }

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

void controlBroadcastThread(safe::signal_t *shutdown_event, control_server_t *server) {
  server->map(packetTypes[IDX_START_A], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_START_A]"sv;
  });

  server->map(packetTypes[IDX_START_B], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_START_B]"sv;
  });

  server->map(packetTypes[IDX_LOSS_STATS], [&](session_t *session, const std::string_view &payload) {
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

  server->map(packetTypes[IDX_INVALIDATE_REF_FRAMES], [&](session_t *session, const std::string_view &payload) {
    auto frames = (std::int64_t *)payload.data();
    auto firstFrame = frames[0];
    auto lastFrame = frames[1];

    BOOST_LOG(debug)
      << "type [IDX_INVALIDATE_REF_FRAMES]"sv << std::endl
      << "firstFrame [" << firstFrame << ']' << std::endl
      << "lastFrame [" << lastFrame << ']';

    session->video.idr_events->raise(std::make_pair(firstFrame, lastFrame));
  });

  server->map(packetTypes[IDX_INPUT_DATA], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_INPUT_DATA]"sv;

    int32_t tagged_cipher_length = util::endian::big(*(int32_t*)payload.data());
    std::string_view tagged_cipher { payload.data() + sizeof(tagged_cipher_length), (size_t)tagged_cipher_length };

    crypto::cipher_t cipher { session->gcm_key };
    cipher.padding = false;

    std::vector<uint8_t> plaintext;
    if(cipher.decrypt_gcm(session->iv, tagged_cipher, plaintext)) {
      // something went wrong :(

      BOOST_LOG(error) << "Failed to verify tag"sv;

      session::stop(*session);
    }

    if(tagged_cipher_length >= 16 + session->iv.size()) {
      std::copy(payload.end() - 16, payload.end(), std::begin(session->iv));
    }

    input::print(plaintext.data());
    input::passthrough(session->input, std::move(plaintext));
  });

  while(!shutdown_event->peek()) {
    {
      auto lg = server->_map_addr_session.lock();

      auto now = std::chrono::steady_clock::now();

      KITTY_WHILE_LOOP(auto pos = std::begin(*server->_map_addr_session), pos != std::end(*server->_map_addr_session), {
        TUPLE_2D_REF(addr, port_session, *pos);
        auto session = port_session.second;

        if(now > session->pingTimeout) {
          BOOST_LOG(info) << addr << ": Ping Timeout"sv;
          session::stop(*session);
        }

        if(session->state.load(std::memory_order_acquire) == session::state_e::STOPPING) {
          pos = server->_map_addr_session->erase(pos);

          enet_peer_disconnect_now(session->control.peer, 0);
          session->controlEnd.raise(true);
          continue;
        }

        ++pos;
      })
    }

    if(proc::proc.running() == -1) {
      BOOST_LOG(debug) << "Process terminated"sv;

      std::uint16_t reason = 0x0100;

      std::array<std::uint16_t, 2> payload;
      payload[0] = packetTypes[IDX_TERMINATION];
      payload[1] = reason;

      server->send(std::string_view {(char*)payload.data(), payload.size()});

      auto lg = server->_map_addr_session.lock();
      for(auto pos = std::begin(*server->_map_addr_session); pos != std::end(*server->_map_addr_session); ++pos) {
        auto session = pos->second.second;
        session->shutdown_event.raise(true);
      }
    }

    server->iterate(500ms);
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
      auto fg = util::fail_guard([&]() {
        sock.async_receive_from(asio::buffer(buf[buf_elem]), peer, 0, recv_func[buf_elem]);
      });

      auto type_str = buf_elem ? "AUDIO"sv : "VIDEO"sv;
      BOOST_LOG(debug) << "Recv: "sv << peer.address().to_string() << ":"sv << peer.port() << " :: " << type_str;


      populate_peer_to_session();

      // No data, yet no error
      if(ec == boost::system::errc::connection_refused || ec == boost::system::errc::connection_reset) {
        return;
      }

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
    };
  };

  recv_func_init(video_sock, 0, peer_to_video_session);
  recv_func_init(audio_sock, 1, peer_to_audio_session);

  video_sock.async_receive_from(asio::buffer(buf[0]), peer, 0, recv_func[0]);
  audio_sock.async_receive_from(asio::buffer(buf[1]), peer, 0, recv_func[1]);

  while(!broadcast_shutdown_event.peek()) {
    io.run();
  }
}

void videoBroadcastThread(safe::signal_t *shutdown_event, udp::socket &sock, video::packet_queue_t packets) {
  while(auto packet = packets->pop()) {
    if(shutdown_event->peek()) {
      break;
    }

    auto session = (session_t*)packet->channel_data;
    auto lowseq = session->video.lowseq;

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

        video_packet->rtp.header = FLAG_EXTENSION;
        video_packet->rtp.sequenceNumber = util::endian::big<uint16_t>(lowseq + fecIndex);
      });

    payload = {(char *) payload_new.data(), payload_new.size()};

    auto shards = fec::encode(payload, blocksize, fecPercentage);
    if(shards.data_shards == 0) {
      BOOST_LOG(info) << "skipping frame..."sv << std::endl;
      continue;
    }

    for (auto x = shards.data_shards; x < shards.size(); ++x) {
      auto *inspect = (video_packet_raw_t *)shards.data(x);

      inspect->packet.frameIndex = packet->pts;
      inspect->packet.fecInfo = (
        x << 12 |
        shards.data_shards << 22 |
        fecPercentage << 4
      );

      inspect->rtp.header = FLAG_EXTENSION;
      inspect->rtp.sequenceNumber = util::endian::big<uint16_t>(lowseq + x);
    }

    for(auto x = 0; x < shards.size(); ++x) {
      sock.send_to(asio::buffer(shards[x]), session->video.peer);
    }

    if(packet->flags & AV_PKT_FLAG_KEY) {
      BOOST_LOG(verbose) << "Key Frame ["sv << packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv;
    }
    else {
      BOOST_LOG(verbose) << "Frame ["sv << packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv << std::endl;
    }

    session->video.lowseq += shards.size();
  }

  shutdown_event->raise(true);
}

void audioBroadcastThread(safe::signal_t *shutdown_event, udp::socket &sock, audio::packet_queue_t packets) {
  while (auto packet = packets->pop()) {
    if(shutdown_event->peek()) {
      break;
    }

    TUPLE_2D_REF(channel_data, packet_data, *packet);
    auto session = (session_t*)channel_data;

    auto frame = session->audio.frame++;

    audio_packet_t audio_packet { (audio_packet_raw_t*)malloc(sizeof(audio_packet_raw_t) + packet_data.size()) };

    audio_packet->rtp.header = 0;
    audio_packet->rtp.packetType = 97;
    audio_packet->rtp.sequenceNumber = util::endian::big(frame);
    audio_packet->rtp.timestamp = 0;
    audio_packet->rtp.ssrc = 0;

    std::copy(std::begin(packet_data), std::end(packet_data), audio_packet->payload());

    sock.send_to(asio::buffer((char*)audio_packet.get(), sizeof(audio_packet_raw_t) + packet_data.size()), session->audio.peer);
    BOOST_LOG(verbose) << "Audio ["sv << frame << "] ::  send..."sv;
  }

  shutdown_event->raise(true);
}

int start_broadcast(broadcast_ctx_t &ctx) {
  if(ctx.control_server.bind(CONTROL_PORT)) {
    BOOST_LOG(error) << "Couldn't bind Control server to port ["sv << CONTROL_PORT << "], likely another process already bound to the port"sv;

    return -1;
  }

  boost::system::error_code ec;
  ctx.video_sock.open(udp::v4(), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't open socket for Video server: "sv << ec.message();

    return -1;
  }

  ctx.video_sock.bind(udp::endpoint(udp::v4(), VIDEO_STREAM_PORT), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't bind Video server to port ["sv << VIDEO_STREAM_PORT << "]: "sv << ec.message();

    return -1;
  }

  ctx.audio_sock.open(udp::v4(), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't open socket for Audio server: "sv << ec.message();

    return -1;
  }

  ctx.audio_sock.bind(udp::endpoint(udp::v4(), AUDIO_STREAM_PORT), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't bind Audio server to port ["sv << AUDIO_STREAM_PORT << "]: "sv << ec.message();

    return -1;
  }

  ctx.video_packets = std::make_shared<video::packet_queue_t::element_type>(30);
  ctx.audio_packets = std::make_shared<audio::packet_queue_t::element_type>(30);
  ctx.message_queue_queue = std::make_shared<message_queue_queue_t::element_type>(30);

  ctx.video_thread = std::thread { videoBroadcastThread, &broadcast_shutdown_event, std::ref(ctx.video_sock), ctx.video_packets };
  ctx.audio_thread = std::thread { audioBroadcastThread, &broadcast_shutdown_event, std::ref(ctx.audio_sock), ctx.audio_packets };
  ctx.control_thread = std::thread { controlBroadcastThread, &broadcast_shutdown_event, &ctx.control_server };

  ctx.recv_thread = std::thread { recvThread, std::ref(ctx) };

  return 0;
}

void end_broadcast(broadcast_ctx_t &ctx) {
  broadcast_shutdown_event.raise(true);

  // Minimize delay stopping video/audio threads
  ctx.video_packets->stop();
  ctx.audio_packets->stop();

  ctx.message_queue_queue->stop();
  ctx.io.stop();

  ctx.video_sock.close();
  ctx.audio_sock.close();

  ctx.video_packets.reset();
  ctx.audio_packets.reset();

  BOOST_LOG(debug) << "Waiting for main listening thread to end..."sv;
  ctx.recv_thread.join();
  BOOST_LOG(debug) << "Waiting for main video thread to end..."sv;
  ctx.video_thread.join();
  BOOST_LOG(debug) << "Waiting for main audio thread to end..."sv;
  ctx.audio_thread.join();
  BOOST_LOG(debug) << "Waiting for main control thread to end..."sv;
  ctx.control_thread.join();
  BOOST_LOG(debug) << "All broadcasting threads ended"sv;

  broadcast_shutdown_event.reset();
}

int recv_ping(decltype(broadcast)::ptr_t ref, socket_e type, asio::ip::address &addr, std::chrono::milliseconds timeout) {
  auto constexpr ping = "PING"sv;

  auto messages = std::make_shared<message_queue_t::element_type>(30);
  ref->message_queue_queue->raise(type, addr, messages);

  auto fg = util::fail_guard([&]() {
    // remove message queue from session
    ref->message_queue_queue->raise(type, addr, nullptr);
  });

  auto msg_opt = messages->pop(config::stream.ping_timeout);
  messages->stop();

  if(!msg_opt) {
    BOOST_LOG(error) << "Initial Ping Timeout"sv;

    return -1;
  }

  TUPLE_2D_REF(port, msg, *msg_opt);
  if(msg != ping) {
    BOOST_LOG(error) << "First message is not a PING";
    BOOST_LOG(debug) << "Received from "sv << addr << ':' << port << " ["sv << util::hex_vec(msg) << ']';

    return -1;
  }

  return port;
}

void videoThread(session_t *session, std::string addr_str) {
  auto fg = util::fail_guard([&]() {
    session::stop(*session);
  });

  while_starting_do_nothing(session->state);

  auto addr = asio::ip::make_address(addr_str);
  auto ref = broadcast.ref();
  auto port = recv_ping(ref, socket_e::video, addr, config::stream.ping_timeout);
  if(port < 0) {
    return;
  }

  session->video.peer.address(addr);
  session->video.peer.port(port);

  BOOST_LOG(debug) << "Start capturing Video"sv;
  video::capture(&session->shutdown_event, ref->video_packets, session->video.idr_events, session->config.monitor, session);
}

void audioThread(session_t *session, std::string addr_str) {
  auto fg = util::fail_guard([&]() {
    session::stop(*session);
  });

  while_starting_do_nothing(session->state);

  auto addr = asio::ip::make_address(addr_str);

  auto ref = broadcast.ref();
  auto port = recv_ping(ref, socket_e::audio, addr, config::stream.ping_timeout);
  if(port < 0) {
    return;
  }

  session->audio.peer.address(addr);
  session->audio.peer.port(port);

  BOOST_LOG(debug) << "Start capturing Audio"sv;
  audio::capture(&session->shutdown_event, ref->audio_packets, session->config.audio, session);
}

namespace session {
state_e state(session_t &session) {
  return session.state.load(std::memory_order_relaxed);
}

void stop(session_t &session) {
  while_starting_do_nothing(session.state);

  auto expected = state_e::RUNNING;
  auto already_stopping = !session.state.compare_exchange_strong(expected, state_e::STOPPING);
  if(already_stopping) {
    return;
  }

  session.shutdown_event.raise(true);
}

void join(session_t &session) {
  BOOST_LOG(debug) << "Waiting for video to end..."sv;
  session.videoThread.join();
  BOOST_LOG(debug) << "Waiting for audio to end..."sv;
  session.audioThread.join();
  BOOST_LOG(debug) << "Waiting for control to end..."sv;
  session.controlEnd.view();
  BOOST_LOG(debug) << "Session ended"sv;
}

int start(session_t &session, const std::string &addr_string) {
  session.input = input::alloc();

  session.broadcast_ref = broadcast.ref();
  if(!session.broadcast_ref) {
    return -1;
  }

  session.broadcast_ref->control_server.emplace_addr_to_session(addr_string, session);

  session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

  session.audioThread = std::thread {audioThread, &session, addr_string};
  session.videoThread = std::thread {videoThread, &session, addr_string};

  session.state.store(state_e::RUNNING, std::memory_order_relaxed);

  return 0;
}

std::shared_ptr<session_t> alloc(config_t &config, crypto::aes_t &gcm_key, crypto::aes_t &iv) {
  auto session = std::make_shared<session_t>();

  session->config = config;
  session->gcm_key = gcm_key;
  session->iv = iv;

  session->video.idr_events = std::make_shared<video::idr_event_t::element_type>();
  session->video.lowseq = 0;

  session->audio.frame = 1;

  session->control.peer = nullptr;
  session->state.store(state_e::STOPPED, std::memory_order_relaxed);

  return session;
}
}
}
