// Created by loki on 6/5/19.

#include "process.h"

#include <future>
#include <queue>

#include <fstream>
#include <openssl/err.h>

extern "C" {
#include <moonlight-common-c/src/RtpAudioQueue.h>
#include <moonlight-common-c/src/Video.h>
#include <rs.h>
}

#include "config.h"
#include "input.h"
#include "main.h"
#include "network.h"
#include "stream.h"
#include "sync.h"
#include "thread_safe.h"
#include "utility.h"

#define IDX_START_A 0
#define IDX_START_B 1
#define IDX_INVALIDATE_REF_FRAMES 2
#define IDX_LOSS_STATS 3
#define IDX_INPUT_DATA 5
#define IDX_RUMBLE_DATA 6
#define IDX_TERMINATION 7
#define IDX_PERIODIC_PING 8
#define IDX_REQUEST_IDR_FRAME 9
#define IDX_ENCRYPTED 10
#define IDX_HDR_MODE 11

static const short packetTypes[] = {
  0x0305, // Start A
  0x0307, // Start B
  0x0301, // Invalidate reference frames
  0x0201, // Loss Stats
  0x0204, // Frame Stats (unused)
  0x0206, // Input data
  0x010b, // Rumble data
  0x0100, // Termination
  0x0200, // Periodic Ping
  0x0302, // IDR frame
  0x0001, // fully encrypted
  0x010e, // HDR mode
};

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

struct video_short_frame_header_t {
  uint8_t *payload() {
    return (uint8_t *)(this + 1);
  }

  std::uint8_t headerType; // Always 0x01 for short headers
  std::uint8_t unknown[2];

  // Currently known values:
  // 1 = Normal P-frame
  // 2 = IDR-frame
  // 4 = P-frame with intra-refresh blocks
  // 5 = P-frame after reference frame invalidation
  std::uint8_t frameType;

  std::uint8_t unknown2[4];
};

static_assert(
  sizeof(video_short_frame_header_t) == 8,
  "Short frame header must be 8 bytes");

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

struct control_header_v2 {
  std::uint16_t type;
  std::uint16_t payloadLength;

  uint8_t *payload() {
    return (uint8_t *)(this + 1);
  }
};

struct control_terminate_t {
  control_header_v2 header;

  std::uint32_t ec;
};

struct control_rumble_t {
  control_header_v2 header;

  std::uint32_t useless;

  std::uint16_t id;
  std::uint16_t lowfreq;
  std::uint16_t highfreq;
};

struct control_hdr_mode_t {
  control_header_v2 header;

  std::uint8_t enabled;

  // Sunshine protocol extension
  SS_HDR_METADATA metadata;
};

typedef struct control_encrypted_t {
  std::uint16_t encryptedHeaderType; // Always LE 0x0001
  std::uint16_t length;              // sizeof(seq) + 16 byte tag + secondary header and data

  // seq is accepted as an arbitrary value in Moonlight
  std::uint32_t seq; // Monotonically increasing sequence number (used as IV for AES-GCM)

  uint8_t *payload() {
    return (uint8_t *)(this + 1);
  }
  // encrypted control_header_v2 and payload data follow
} *control_encrypted_p;

struct audio_fec_packet_raw_t {
  uint8_t *payload() {
    return (uint8_t *)(this + 1);
  }

  RTP_PACKET rtp;
  AUDIO_FEC_HEADER fecHeader;
};

#pragma pack(pop)

constexpr std::size_t round_to_pkcs7_padded(std::size_t size) {
  return ((size + 15) / 16) * 16;
}
constexpr std::size_t MAX_AUDIO_PACKET_SIZE = 1400;

using rh_t               = util::safe_ptr<reed_solomon, reed_solomon_release>;
using video_packet_t     = util::c_ptr<video_packet_raw_t>;
using audio_packet_t     = util::c_ptr<audio_packet_raw_t>;
using audio_fec_packet_t = util::c_ptr<audio_fec_packet_raw_t>;
using audio_aes_t        = std::array<char, round_to_pkcs7_padded(MAX_AUDIO_PACKET_SIZE)>;

using message_queue_t       = std::shared_ptr<safe::queue_t<std::pair<std::uint16_t, std::string>>>;
using message_queue_queue_t = std::shared_ptr<safe::queue_t<std::tuple<socket_e, asio::ip::address, message_queue_t>>>;

// return bytes written on success
// return -1 on error
static inline int encode_audio(int featureSet, const audio::buffer_t &plaintext, audio_packet_t &destination, std::uint32_t avRiKeyIv, crypto::cipher::cbc_t &cbc) {
  // If encryption isn't enabled
  if(!(featureSet & 0x20)) {
    std::copy(std::begin(plaintext), std::end(plaintext), destination->payload());
    return plaintext.size();
  }

  crypto::aes_t iv {};
  *(std::uint32_t *)iv.data() = util::endian::big<std::uint32_t>(avRiKeyIv + destination->rtp.sequenceNumber);

  return cbc.encrypt(std::string_view { (char *)std::begin(plaintext), plaintext.size() }, destination->payload(), &iv);
}

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

  void call(std::uint16_t type, session_t *session, const std::string_view &payload);

  void map(uint16_t type, std::function<void(session_t *, const std::string_view &)> cb) {
    _map_type_cb.emplace(type, std::move(cb));
  }

  int send(const std::string_view &payload, net::peer_t peer) {
    auto packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
    if(enet_peer_send(peer, 0, packet)) {
      enet_packet_destroy(packet);

      return -1;
    }

    return 0;
  }

  void flush() {
    enet_host_flush(_host.get());
  }

  // Callbacks
  std::unordered_map<std::uint16_t, std::function<void(session_t *, const std::string_view &)>> _map_type_cb;

  // Mapping ip:port to session
  util::sync_t<std::unordered_multimap<std::string, std::pair<std::uint16_t, session_t *>>> _map_addr_session;

  ENetAddress _addr;
  net::host_t _host;
};

struct broadcast_ctx_t {
  message_queue_queue_t message_queue_queue;

  std::thread recv_thread;
  std::thread video_thread;
  std::thread audio_thread;
  std::thread control_thread;

  asio::io_service io;

  udp::socket video_sock { io };
  udp::socket audio_sock { io };

  // This is purely for administrative purposes.
  //   It's possible two instances of Moonlight are behind a NAT.
  //   From Sunshine's point of view, the ip addresses are identical
  //   We need some way to know what ports are already used for different streams
  util::sync_t<std::vector<std::pair<std::string, std::uint16_t>>> audio_video_connections;

  control_server_t control_server;
};

struct session_t {
  config_t config;

  safe::mail_t mail;

  std::shared_ptr<input::input_t> input;

  std::thread audioThread;
  std::thread videoThread;

  std::chrono::steady_clock::time_point pingTimeout;

  safe::shared_t<broadcast_ctx_t>::ptr_t broadcast_ref;

  struct {
    int lowseq;
    udp::endpoint peer;
    safe::mail_raw_t::event_t<bool> idr_events;
    std::unique_ptr<platf::deinit_t> qos;
  } video;

  struct {
    crypto::cipher::cbc_t cipher;

    std::uint16_t sequenceNumber;
    // avRiKeyId == util::endian::big(First (sizeof(avRiKeyId)) bytes of launch_session->iv)
    std::uint32_t avRiKeyId;
    std::uint32_t timestamp;
    udp::endpoint peer;

    util::buffer_t<char> shards;
    util::buffer_t<uint8_t *> shards_p;

    audio_fec_packet_t fec_packet;
    std::unique_ptr<platf::deinit_t> qos;
  } audio;

  struct {
    crypto::cipher::gcm_t cipher;
    crypto::aes_t iv;

    net::peer_t peer;
    std::uint8_t seq;

    platf::rumble_queue_t rumble_queue;
    safe::mail_raw_t::event_t<video::hdr_info_t> hdr_queue;
  } control;

  safe::mail_raw_t::event_t<bool> shutdown_event;
  safe::signal_t controlEnd;

  std::atomic<session::state_e> state;
};

/**
 * First part of cipher must be struct of type control_encrypted_t
 * 
 * returns empty string_view on failure
 * returns string_view pointing to payload data
 */
template<std::size_t max_payload_size>
static inline std::string_view encode_control(session_t *session, const std::string_view &plaintext, std::array<std::uint8_t, max_payload_size> &tagged_cipher) {
  static_assert(
    max_payload_size >= sizeof(control_encrypted_t) + sizeof(crypto::cipher::tag_size),
    "max_payload_size >= sizeof(control_encrypted_t) + sizeof(crypto::cipher::tag_size)");


  if(session->config.controlProtocolType != 13) {
    return plaintext;
  }

  crypto::aes_t iv {};
  auto seq = session->control.seq++;
  iv[0]    = seq;

  auto packet = (control_encrypted_p)tagged_cipher.data();

  auto bytes = session->control.cipher.encrypt(plaintext, packet->payload(), &iv);
  if(bytes <= 0) {
    BOOST_LOG(error) << "Couldn't encrypt control data"sv;
    return {};
  }

  std::uint16_t packet_length = bytes + crypto::cipher::tag_size + sizeof(control_encrypted_t::seq);

  packet->encryptedHeaderType = util::endian::little(0x0001);
  packet->length              = util::endian::little(packet_length);
  packet->seq                 = util::endian::little(seq);

  return std::string_view { (char *)tagged_cipher.data(), packet_length + sizeof(control_encrypted_t) - sizeof(control_encrypted_t::seq) };
}

int start_broadcast(broadcast_ctx_t &ctx);
void end_broadcast(broadcast_ctx_t &ctx);


static auto broadcast = safe::make_shared<broadcast_ctx_t>(start_broadcast, end_broadcast);

session_t *control_server_t::get_session(const net::peer_t peer) {
  TUPLE_2D(port, addr_string, platf::from_sockaddr_ex((sockaddr *)&peer->address.address));

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
    session_port            = port;

    return session_p;
  }

  return nullptr;
}

void control_server_t::call(std::uint16_t type, session_t *session, const std::string_view &payload) {
  auto cb = _map_type_cb.find(type);
  if(cb == std::end(_map_type_cb)) {
    BOOST_LOG(debug)
      << "type [Unknown] { "sv << util::hex(type).to_string_view() << " }"sv << std::endl
      << "---data---"sv << std::endl
      << util::hex_vec(payload) << std::endl
      << "---end data---"sv;
  }
  else {
    cb->second(session, payload);
  }
}

void control_server_t::iterate(std::chrono::milliseconds timeout) {
  ENetEvent event;
  auto res = enet_host_service(_host.get(), &event, timeout.count());

  if(res > 0) {
    auto session = get_session(event.peer);
    if(!session) {
      BOOST_LOG(warning) << "Rejected connection from ["sv << platf::from_sockaddr((sockaddr *)&event.peer->address.address) << "]: it's not properly set up"sv;
      enet_peer_disconnect_now(event.peer, 0);

      return;
    }

    session->pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    switch(event.type) {
    case ENET_EVENT_TYPE_RECEIVE: {
      net::packet_t packet { event.packet };

      auto type = *(std::uint16_t *)packet->data;
      std::string_view payload { (char *)packet->data + sizeof(type), packet->dataLength - sizeof(type) };

      call(type, session, payload);
    } break;
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
    return &shards[el * blocksize];
  }

  std::string_view operator[](size_t el) const {
    return { &shards[el * blocksize], blocksize };
  }

  size_t size() const {
    return nr_shards;
  }
};

static fec_t encode(const std::string_view &payload, size_t blocksize, size_t fecpercentage, size_t minparityshards) {
  auto payload_size = payload.size();

  auto pad = payload_size % blocksize != 0;

  auto data_shards   = payload_size / blocksize + (pad ? 1 : 0);
  auto parity_shards = (data_shards * fecpercentage + 99) / 100;

  // increase the FEC percentage for this frame if the parity shard minimum is not met
  if(parity_shards < minparityshards) {
    parity_shards = minparityshards;
    fecpercentage = (100 * parity_shards) / data_shards;

    BOOST_LOG(verbose) << "Increasing FEC percentage to "sv << fecpercentage << " to meet parity shard minimum"sv << std::endl;
  }

  auto nr_shards = data_shards + parity_shards;
  if(nr_shards > DATA_SHARDS_MAX) {
    BOOST_LOG(warning)
      << "Number of fragments for reed solomon exceeds DATA_SHARDS_MAX"sv << std::endl
      << nr_shards << " > "sv << DATA_SHARDS_MAX
      << ", skipping error correction"sv;

    nr_shards     = data_shards;
    fecpercentage = 0;
  }

  util::buffer_t<char> shards { nr_shards * blocksize };
  util::buffer_t<uint8_t *> shards_p { nr_shards };

  // copy payload + padding
  auto next = std::copy(std::begin(payload), std::end(payload), std::begin(shards));
  std::fill(next, std::end(shards), 0); // padding with zero

  for(auto x = 0; x < nr_shards; ++x) {
    shards_p[x] = (uint8_t *)&shards[x * blocksize];
  }

  if(data_shards + parity_shards <= DATA_SHARDS_MAX) {
    // packets = parity_shards + data_shards
    rs_t rs { reed_solomon_new(data_shards, parity_shards) };

    reed_solomon_encode(rs.get(), shards_p.begin(), nr_shards, blocksize);
  }

  return {
    data_shards,
    nr_shards,
    fecpercentage,
    blocksize,
    std::move(shards)
  };
}
} // namespace fec

template<class F>
std::vector<uint8_t> insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data, F &&f) {
  auto pad      = data.size() % slice_size != 0;
  auto elements = data.size() / slice_size + (pad ? 1 : 0);

  std::vector<uint8_t> result;
  result.resize(elements * insert_size + data.size());

  auto next = std::begin(data);
  for(auto x = 0; x < elements - 1; ++x) {
    void *p = &result[x * (insert_size + slice_size)];

    f(p, x, elements);

    std::copy(next, next + slice_size, (char *)p + insert_size);
    next += slice_size;
  }

  auto x  = elements - 1;
  void *p = &result[x * (insert_size + slice_size)];

  f(p, x, elements);

  std::copy(next, std::end(data), (char *)p + insert_size);

  return result;
}

std::vector<uint8_t> replace(const std::string_view &original, const std::string_view &old, const std::string_view &_new) {
  std::vector<uint8_t> replaced;

  auto begin = std::begin(original);
  auto end   = std::end(original);
  auto next  = std::search(begin, end, std::begin(old), std::end(old));

  std::copy(begin, next, std::back_inserter(replaced));
  if(next != end) {
    std::copy(std::begin(_new), std::end(_new), std::back_inserter(replaced));
    std::copy(next + old.size(), end, std::back_inserter(replaced));
  }

  return replaced;
}

int send_rumble(session_t *session, std::uint16_t id, std::uint16_t lowfreq, std::uint16_t highfreq) {
  if(!session->control.peer) {
    BOOST_LOG(warning) << "Couldn't send rumble data, still waiting for PING from Moonlight"sv;
    // Still waiting for PING from Moonlight
    return -1;
  }

  control_rumble_t plaintext;
  plaintext.header.type          = packetTypes[IDX_RUMBLE_DATA];
  plaintext.header.payloadLength = sizeof(control_rumble_t) - sizeof(control_header_v2);

  plaintext.useless  = 0xC0FFEE;
  plaintext.id       = util::endian::little(id);
  plaintext.lowfreq  = util::endian::little(lowfreq);
  plaintext.highfreq = util::endian::little(highfreq);

  BOOST_LOG(verbose) << id << " :: "sv << util::hex(lowfreq).to_string_view() << " :: "sv << util::hex(highfreq).to_string_view();
  std::array<std::uint8_t,
    sizeof(control_encrypted_t) + crypto::cipher::round_to_pkcs7_padded(sizeof(plaintext)) + crypto::cipher::tag_size>
    encrypted_payload;

  auto payload = encode_control(session, util::view(plaintext), encrypted_payload);
  if(session->broadcast_ref->control_server.send(payload, session->control.peer)) {
    TUPLE_2D(port, addr, platf::from_sockaddr_ex((sockaddr *)&session->control.peer->address.address));
    BOOST_LOG(warning) << "Couldn't send termination code to ["sv << addr << ':' << port << ']';

    return -1;
  }

  BOOST_LOG(debug) << "Send gamepadnr ["sv << id << "] with lowfreq ["sv << lowfreq << "] and highfreq ["sv << highfreq << ']';

  return 0;
}

int send_hdr_mode(session_t *session, video::hdr_info_t hdr_info) {
  if(!session->control.peer) {
    BOOST_LOG(warning) << "Couldn't send HDR mode, still waiting for PING from Moonlight"sv;
    // Still waiting for PING from Moonlight
    return -1;
  }

  control_hdr_mode_t plaintext {};
  plaintext.header.type          = packetTypes[IDX_HDR_MODE];
  plaintext.header.payloadLength = sizeof(control_hdr_mode_t) - sizeof(control_header_v2);

  plaintext.enabled  = hdr_info->enabled;
  plaintext.metadata = hdr_info->metadata;

  std::array<std::uint8_t,
    sizeof(control_encrypted_t) + crypto::cipher::round_to_pkcs7_padded(sizeof(plaintext)) + crypto::cipher::tag_size>
    encrypted_payload;

  auto payload = encode_control(session, util::view(plaintext), encrypted_payload);
  if(session->broadcast_ref->control_server.send(payload, session->control.peer)) {
    TUPLE_2D(port, addr, platf::from_sockaddr_ex((sockaddr *)&session->control.peer->address.address));
    BOOST_LOG(warning) << "Couldn't send HDR mode to ["sv << addr << ':' << port << ']';

    return -1;
  }

  BOOST_LOG(debug) << "Sent HDR mode: " << hdr_info->enabled;
  return 0;
}

void controlBroadcastThread(control_server_t *server) {
  server->map(packetTypes[IDX_PERIODIC_PING], [](session_t *session, const std::string_view &payload) {
    BOOST_LOG(verbose) << "type [IDX_START_A]"sv;
  });

  server->map(packetTypes[IDX_START_A], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_START_A]"sv;
  });

  server->map(packetTypes[IDX_START_B], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_START_B]"sv;
  });

  server->map(packetTypes[IDX_LOSS_STATS], [&](session_t *session, const std::string_view &payload) {
    int32_t *stats = (int32_t *)payload.data();
    auto count     = stats[0];
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

  server->map(packetTypes[IDX_REQUEST_IDR_FRAME], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_REQUEST_IDR_FRAME]"sv;

    session->video.idr_events->raise(true);
  });

  server->map(packetTypes[IDX_INVALIDATE_REF_FRAMES], [&](session_t *session, const std::string_view &payload) {
    auto frames     = (std::int64_t *)payload.data();
    auto firstFrame = frames[0];
    auto lastFrame  = frames[1];

    BOOST_LOG(debug)
      << "type [IDX_INVALIDATE_REF_FRAMES]"sv << std::endl
      << "firstFrame [" << firstFrame << ']' << std::endl
      << "lastFrame [" << lastFrame << ']';

    session->video.idr_events->raise(true);
  });

  server->map(packetTypes[IDX_INPUT_DATA], [&](session_t *session, const std::string_view &payload) {
    BOOST_LOG(debug) << "type [IDX_INPUT_DATA]"sv;

    auto tagged_cipher_length = util::endian::big(*(int32_t *)payload.data());
    std::string_view tagged_cipher { payload.data() + sizeof(tagged_cipher_length), (size_t)tagged_cipher_length };

    std::vector<uint8_t> plaintext;

    auto &cipher = session->control.cipher;
    auto &iv     = session->control.iv;
    if(cipher.decrypt(tagged_cipher, plaintext, &iv)) {
      // something went wrong :(

      BOOST_LOG(error) << "Failed to verify tag"sv;

      session::stop(*session);
      return;
    }

    if(tagged_cipher_length >= 16 + sizeof(crypto::aes_t)) {
      std::copy(payload.end() - 16, payload.end(), std::begin(iv));
    }

    input::print(plaintext.data());
    input::passthrough(session->input, std::move(plaintext));
  });

  server->map(packetTypes[IDX_ENCRYPTED], [server](session_t *session, const std::string_view &payload) {
    BOOST_LOG(verbose) << "type [IDX_ENCRYPTED]"sv;

    auto header = (control_encrypted_p)(payload.data() - 2);

    auto length = util::endian::little(header->length);
    auto seq    = util::endian::little(header->seq);

    if(length < (16 + 4 + 4)) {
      BOOST_LOG(warning) << "Control: Runt packet"sv;
      return;
    }

    auto tagged_cipher_length = length - 4;
    std::string_view tagged_cipher { (char *)header->payload(), (size_t)tagged_cipher_length };

    auto &cipher = session->control.cipher;
    crypto::aes_t iv {};
    iv[0] = (std::uint8_t)seq;

    // update control sequence
    ++session->control.seq;

    std::vector<uint8_t> plaintext;
    if(cipher.decrypt(tagged_cipher, plaintext, &iv)) {
      // something went wrong :(

      BOOST_LOG(error) << "Failed to verify tag"sv;

      session::stop(*session);
      return;
    }

    // Ensure compatibility with old packet type
    std::string_view next_payload { (char *)plaintext.data(), plaintext.size() };
    auto type = *(std::uint16_t *)next_payload.data();

    if(type == packetTypes[IDX_ENCRYPTED]) {
      BOOST_LOG(error) << "Bad packet type [IDX_ENCRYPTED] found"sv;

      session::stop(*session);
      return;
    }

    // IDX_INPUT_DATA will attempt to decrypt unencrypted data, therefore we need to skip it.
    if(type != packetTypes[IDX_INPUT_DATA]) {
      server->call(type, session, next_payload);

      return;
    }

    // Ensure compatibility with IDX_INPUT_DATA
    constexpr auto skip = sizeof(std::uint16_t) * 2;
    plaintext.erase(std::begin(plaintext), std::begin(plaintext) + skip);

    input::print(plaintext.data());
    input::passthrough(session->input, std::move(plaintext));
  });

  // This thread handles latency-sensitive control messages
  platf::adjust_thread_priority(platf::thread_priority_e::critical);

  auto shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);
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

          if(session->control.peer) {
            enet_peer_disconnect_now(session->control.peer, 0);
          }

          session->controlEnd.raise(true);
          continue;
        }

        auto &rumble_queue = session->control.rumble_queue;
        while(rumble_queue->peek()) {
          auto rumble = rumble_queue->pop();

          send_rumble(session, rumble->id, rumble->lowfreq, rumble->highfreq);
        }

        // Unlike rumble which we send as best-effort, HDR state messages are critical
        // for proper functioning of some clients. We must wait to pop entries from
        // the queue until we're sure we have a peer to send them to.
        auto &hdr_queue = session->control.hdr_queue;
        while(session->control.peer && hdr_queue->peek()) {
          auto hdr_info = hdr_queue->pop();

          send_hdr_mode(session, std::move(hdr_info));
        }

        ++pos;
      })
    }

    if(proc::proc.running() == 0) {
      BOOST_LOG(debug) << "Process terminated"sv;

      break;
    }

    server->iterate(150ms);
  }

  // Let all remaining connections know the server is shutting down
  // reason: graceful termination
  std::uint32_t reason = 0x80030023;

  control_terminate_t plaintext;
  plaintext.header.type          = packetTypes[IDX_TERMINATION];
  plaintext.header.payloadLength = sizeof(plaintext.ec);
  plaintext.ec                   = reason;

  std::array<std::uint8_t,
    sizeof(control_encrypted_t) + crypto::cipher::round_to_pkcs7_padded(sizeof(plaintext)) + crypto::cipher::tag_size>
    encrypted_payload;

  auto lg = server->_map_addr_session.lock();
  for(auto pos = std::begin(*server->_map_addr_session); pos != std::end(*server->_map_addr_session); ++pos) {
    auto session = pos->second.second;

    auto payload = encode_control(session, util::view(plaintext), encrypted_payload);

    if(server->send(payload, session->control.peer)) {
      TUPLE_2D(port, addr, platf::from_sockaddr_ex((sockaddr *)&session->control.peer->address.address));
      BOOST_LOG(warning) << "Couldn't send termination code to ["sv << addr << ':' << port << ']';
    }

    session->shutdown_event->raise(true);
    session->controlEnd.raise(true);
  }

  server->flush();
}

void recvThread(broadcast_ctx_t &ctx) {
  std::map<asio::ip::address, message_queue_t> peer_to_video_session;
  std::map<asio::ip::address, message_queue_t> peer_to_audio_session;

  auto &video_sock = ctx.video_sock;
  auto &audio_sock = ctx.audio_sock;

  auto &message_queue_queue     = ctx.message_queue_queue;
  auto broadcast_shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);

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
    recv_func[buf_elem] = [&, buf_elem](const boost::system::error_code &ec, size_t bytes) {
      auto fg = util::fail_guard([&]() {
        sock.async_receive_from(asio::buffer(buf[buf_elem]), peer, 0, recv_func[buf_elem]);
      });

      auto type_str = buf_elem ? "AUDIO"sv : "VIDEO"sv;
      BOOST_LOG(verbose) << "Recv: "sv << peer.address().to_string() << ':' << peer.port() << " :: " << type_str;


      populate_peer_to_session();

      // No data, yet no error
      if(ec == boost::system::errc::connection_refused || ec == boost::system::errc::connection_reset) {
        return;
      }

      if(ec || !bytes) {
        BOOST_LOG(error) << "Couldn't receive data from udp socket: "sv << ec.message();
        return;
      }

      auto it = peer_to_session.find(peer.address());
      if(it != std::end(peer_to_session)) {
        BOOST_LOG(debug) << "RAISE: "sv << peer.address().to_string() << ':' << peer.port() << " :: " << type_str;
        it->second->raise(peer.port(), std::string { buf[buf_elem].data(), bytes });
      }
    };
  };

  recv_func_init(video_sock, 0, peer_to_video_session);
  recv_func_init(audio_sock, 1, peer_to_audio_session);

  video_sock.async_receive_from(asio::buffer(buf[0]), peer, 0, recv_func[0]);
  audio_sock.async_receive_from(asio::buffer(buf[1]), peer, 0, recv_func[1]);

  while(!broadcast_shutdown_event->peek()) {
    io.run();
  }
}

void videoBroadcastThread(udp::socket &sock) {
  auto shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);
  auto packets        = mail::man->queue<video::packet_t>(mail::video_packets);
  auto timebase       = boost::posix_time::microsec_clock::universal_time();

  // Video traffic is sent on this thread
  platf::adjust_thread_priority(platf::thread_priority_e::high);

  while(auto packet = packets->pop()) {
    if(shutdown_event->peek()) {
      break;
    }

    auto session = (session_t *)packet->channel_data;
    auto lowseq  = session->video.lowseq;

    auto av_packet = packet->av_packet;
    std::string_view payload { (char *)av_packet->data, (size_t)av_packet->size };
    std::vector<uint8_t> payload_new;

    video_short_frame_header_t frame_header = {};
    frame_header.headerType                 = 0x01; // Short header type
    frame_header.frameType                  = (av_packet->flags & AV_PKT_FLAG_KEY) ? 2 : 1;

    std::copy_n((uint8_t *)&frame_header, sizeof(frame_header), std::back_inserter(payload_new));
    std::copy(std::begin(payload), std::end(payload), std::back_inserter(payload_new));

    payload = { (char *)payload_new.data(), payload_new.size() };

    if(av_packet->flags & AV_PKT_FLAG_KEY) {
      for(auto &replacement : *packet->replacements) {
        auto frame_old = replacement.old;
        auto frame_new = replacement._new;

        payload_new = replace(payload, frame_old, frame_new);
        payload     = { (char *)payload_new.data(), payload_new.size() };
      }
    }

    // insert packet headers
    auto blocksize         = session->config.packetsize + MAX_RTP_HEADER_SIZE;
    auto payload_blocksize = blocksize - sizeof(video_packet_raw_t);

    auto fecPercentage = config::stream.fec_percentage;

    payload_new = insert(sizeof(video_packet_raw_t), payload_blocksize,
      payload, [&](void *p, int fecIndex, int end) {
        video_packet_raw_t *video_packet = (video_packet_raw_t *)p;

        video_packet->packet.flags = FLAG_CONTAINS_PIC_DATA;
      });

    payload = std::string_view { (char *)payload_new.data(), payload_new.size() };

    // With a fecpercentage of 255, if payload_new is broken up into more than a 100 data_shards
    // it will generate greater than DATA_SHARDS_MAX shards.
    // Therefore, we start breaking the data up into three separate fec blocks.
    auto multi_fec_threshold = 90 * blocksize;

    // We can go up to 4 fec blocks, but 3 is plenty
    constexpr auto MAX_FEC_BLOCKS = 3;

    std::array<std::string_view, MAX_FEC_BLOCKS> fec_blocks;
    decltype(fec_blocks)::iterator
      fec_blocks_begin = std::begin(fec_blocks),
      fec_blocks_end   = std::begin(fec_blocks) + 1;

    auto lastBlockIndex = 0;
    if(payload.size() > multi_fec_threshold) {
      BOOST_LOG(verbose) << "Generating multiple FEC blocks"sv;

      // Align individual fec blocks to blocksize
      auto unaligned_size = payload.size() / MAX_FEC_BLOCKS;
      auto aligned_size   = ((unaligned_size + (blocksize - 1)) / blocksize) * blocksize;

      // Break the data up into 3 blocks, each containing multiple complete video packets.
      fec_blocks[0] = payload.substr(0, aligned_size);
      fec_blocks[1] = payload.substr(aligned_size, aligned_size);
      fec_blocks[2] = payload.substr(aligned_size * 2);

      lastBlockIndex = 2 << 6;
      fec_blocks_end = std::end(fec_blocks);
    }
    else {
      BOOST_LOG(verbose) << "Generating single FEC block"sv;
      fec_blocks[0] = payload;
    }

    try {
      auto blockIndex = 0;
      std::for_each(fec_blocks_begin, fec_blocks_end, [&](std::string_view &current_payload) {
        auto packets = (current_payload.size() + (blocksize - 1)) / blocksize;

        for(int x = 0; x < packets; ++x) {
          auto *inspect  = (video_packet_raw_t *)&current_payload[x * blocksize];
          auto av_packet = packet->av_packet;

          inspect->packet.frameIndex        = av_packet->pts;
          inspect->packet.streamPacketIndex = ((uint32_t)lowseq + x) << 8;

          // Match multiFecFlags with Moonlight
          inspect->packet.multiFecFlags  = 0x10;
          inspect->packet.multiFecBlocks = (blockIndex << 4) | lastBlockIndex;

          if(x == 0) {
            inspect->packet.flags |= FLAG_SOF;
          }

          if(x == packets - 1) {
            inspect->packet.flags |= FLAG_EOF;
          }
        }

        auto shards = fec::encode(current_payload, blocksize, fecPercentage, session->config.minRequiredFecPackets);

        // set FEC info now that we know for sure what our percentage will be for this frame
        for(auto x = 0; x < shards.size(); ++x) {
          auto *inspect = (video_packet_raw_t *)shards.data(x);

          // RTP video timestamps use a 90 KHz clock
          auto now       = boost::posix_time::microsec_clock::universal_time();
          auto timestamp = (now - timebase).total_microseconds() / (1000 / 90);

          inspect->packet.fecInfo =
            (x << 12 |
              shards.data_shards << 22 |
              shards.percentage << 4);

          inspect->rtp.header         = 0x80 | FLAG_EXTENSION;
          inspect->rtp.sequenceNumber = util::endian::big<uint16_t>(lowseq + x);
          inspect->rtp.timestamp      = util::endian::big<uint32_t>(timestamp);

          inspect->packet.multiFecBlocks = (blockIndex << 4) | lastBlockIndex;
          inspect->packet.frameIndex     = av_packet->pts;
        }

        auto peer_address = session->video.peer.address();
        auto batch_info   = platf::batched_send_info_t {
          shards.shards.begin(),
          shards.blocksize,
          shards.nr_shards,
          (uintptr_t)sock.native_handle(),
          peer_address,
          session->video.peer.port(),
        };

        // Use a batched send if it's supported on this platform
        if(!platf::send_batch(batch_info)) {
          // Batched send is not available, so send each packet individually
          BOOST_LOG(verbose) << "Falling back to unbatched send"sv;
          for(auto x = 0; x < shards.size(); ++x) {
            sock.send_to(asio::buffer(shards[x]), session->video.peer);
          }
        }

        if(av_packet->flags & AV_PKT_FLAG_KEY) {
          BOOST_LOG(verbose) << "Key Frame ["sv << av_packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv;
        }
        else {
          BOOST_LOG(verbose) << "Frame ["sv << av_packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv << std::endl;
        }

        ++blockIndex;
        lowseq += shards.size();
      });

      session->video.lowseq = lowseq;
    }
    catch(const std::exception &e) {
      BOOST_LOG(error) << "Broadcast video failed "sv << e.what();
      std::this_thread::sleep_for(100ms);
    }
  }

  shutdown_event->raise(true);
}

void audioBroadcastThread(udp::socket &sock) {
  auto shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);
  auto packets        = mail::man->queue<audio::packet_t>(mail::audio_packets);

  constexpr auto max_block_size = crypto::cipher::round_to_pkcs7_padded(2048);

  audio_packet_t audio_packet { (audio_packet_raw_t *)malloc(sizeof(audio_packet_raw_t) + max_block_size) };
  fec::rs_t rs { reed_solomon_new(RTPA_DATA_SHARDS, RTPA_FEC_SHARDS) };

  // For unknown reasons, the RS parity matrix computed by our RS implementation
  // doesn't match the one Nvidia uses for audio data. I'm not exactly sure why,
  // but we can simply replace it with the matrix generated by OpenFEC which
  // works correctly. This is possible because the data and FEC shard count is
  // constant and known in advance.
  const unsigned char parity[] = { 0x77, 0x40, 0x38, 0x0e, 0xc7, 0xa7, 0x0d, 0x6c };
  memcpy(rs.get()->p, parity, sizeof(parity));

  audio_packet->rtp.header     = 0x80;
  audio_packet->rtp.packetType = 97;
  audio_packet->rtp.ssrc       = 0;

  // Audio traffic is sent on this thread
  platf::adjust_thread_priority(platf::thread_priority_e::high);

  while(auto packet = packets->pop()) {
    if(shutdown_event->peek()) {
      break;
    }

    TUPLE_2D_REF(channel_data, packet_data, *packet);
    auto session = (session_t *)channel_data;

    auto sequenceNumber = session->audio.sequenceNumber;
    auto timestamp      = session->audio.timestamp;

    // This will be mapped to big-endianness later
    // For now, encode_audio needs it to be the proper sequenceNumber
    audio_packet->rtp.sequenceNumber = sequenceNumber;

    auto bytes = encode_audio(session->config.featureFlags, packet_data, audio_packet, session->audio.avRiKeyId, session->audio.cipher);
    if(bytes < 0) {
      BOOST_LOG(error) << "Couldn't encode audio packet"sv;
      break;
    }

    audio_packet->rtp.sequenceNumber = util::endian::big(sequenceNumber);
    audio_packet->rtp.timestamp      = util::endian::big(timestamp);

    session->audio.sequenceNumber++;
    session->audio.timestamp += session->config.audio.packetDuration;

    auto &shards_p = session->audio.shards_p;

    std::copy_n(audio_packet->payload(), bytes, shards_p[sequenceNumber % RTPA_DATA_SHARDS]);
    try {
      sock.send_to(asio::buffer((char *)audio_packet.get(), sizeof(audio_packet_raw_t) + bytes), session->audio.peer);


      BOOST_LOG(verbose) << "Audio ["sv << sequenceNumber << "] ::  send..."sv;

      auto &fec_packet = session->audio.fec_packet;
      // initialize the FEC header at the beginning of the FEC block
      if(sequenceNumber % RTPA_DATA_SHARDS == 0) {
        fec_packet->fecHeader.baseSequenceNumber = util::endian::big(sequenceNumber);
        fec_packet->fecHeader.baseTimestamp      = util::endian::big(timestamp);
      }

      // generate parity shards at the end of the FEC block
      if((sequenceNumber + 1) % RTPA_DATA_SHARDS == 0) {
        reed_solomon_encode(rs.get(), shards_p.begin(), RTPA_TOTAL_SHARDS, bytes);

        for(auto x = 0; x < RTPA_FEC_SHARDS; ++x) {
          fec_packet->rtp.sequenceNumber      = util::endian::big<std::uint16_t>(sequenceNumber + x + 1);
          fec_packet->fecHeader.fecShardIndex = x;
          memcpy(fec_packet->payload(), shards_p[RTPA_DATA_SHARDS + x], bytes);
          sock.send_to(asio::buffer((char *)fec_packet.get(), sizeof(audio_fec_packet_raw_t) + bytes), session->audio.peer);
          BOOST_LOG(verbose) << "Audio FEC ["sv << (sequenceNumber & ~(RTPA_DATA_SHARDS - 1)) << ' ' << x << "] ::  send..."sv;
        }
      }
    }
    catch(const std::exception &e) {
      BOOST_LOG(error) << "Broadcast audio failed "sv << e.what();
      std::this_thread::sleep_for(100ms);
    }
  }

  shutdown_event->raise(true);
}

int start_broadcast(broadcast_ctx_t &ctx) {
  auto control_port = map_port(CONTROL_PORT);
  auto video_port   = map_port(VIDEO_STREAM_PORT);
  auto audio_port   = map_port(AUDIO_STREAM_PORT);

  if(ctx.control_server.bind(control_port)) {
    BOOST_LOG(error) << "Couldn't bind Control server to port ["sv << control_port << "], likely another process already bound to the port"sv;

    return -1;
  }

  boost::system::error_code ec;
  ctx.video_sock.open(udp::v4(), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't open socket for Video server: "sv << ec.message();

    return -1;
  }

  ctx.video_sock.bind(udp::endpoint(udp::v4(), video_port), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't bind Video server to port ["sv << video_port << "]: "sv << ec.message();

    return -1;
  }

  ctx.audio_sock.open(udp::v4(), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't open socket for Audio server: "sv << ec.message();

    return -1;
  }

  ctx.audio_sock.bind(udp::endpoint(udp::v4(), audio_port), ec);
  if(ec) {
    BOOST_LOG(fatal) << "Couldn't bind Audio server to port ["sv << audio_port << "]: "sv << ec.message();

    return -1;
  }

  ctx.message_queue_queue = std::make_shared<message_queue_queue_t::element_type>(30);

  ctx.video_thread   = std::thread { videoBroadcastThread, std::ref(ctx.video_sock) };
  ctx.audio_thread   = std::thread { audioBroadcastThread, std::ref(ctx.audio_sock) };
  ctx.control_thread = std::thread { controlBroadcastThread, &ctx.control_server };

  ctx.recv_thread = std::thread { recvThread, std::ref(ctx) };

  return 0;
}

void end_broadcast(broadcast_ctx_t &ctx) {
  auto broadcast_shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);

  broadcast_shutdown_event->raise(true);

  auto video_packets = mail::man->queue<video::packet_t>(mail::video_packets);
  auto audio_packets = mail::man->queue<audio::packet_t>(mail::audio_packets);

  // Minimize delay stopping video/audio threads
  video_packets->stop();
  audio_packets->stop();

  ctx.message_queue_queue->stop();
  ctx.io.stop();

  ctx.video_sock.close();
  ctx.audio_sock.close();

  video_packets.reset();
  audio_packets.reset();

  BOOST_LOG(debug) << "Waiting for main listening thread to end..."sv;
  ctx.recv_thread.join();
  BOOST_LOG(debug) << "Waiting for main video thread to end..."sv;
  ctx.video_thread.join();
  BOOST_LOG(debug) << "Waiting for main audio thread to end..."sv;
  ctx.audio_thread.join();
  BOOST_LOG(debug) << "Waiting for main control thread to end..."sv;
  ctx.control_thread.join();
  BOOST_LOG(debug) << "All broadcasting threads ended"sv;

  broadcast_shutdown_event->reset();
}

int recv_ping(decltype(broadcast)::ptr_t ref, socket_e type, udp::endpoint &peer, std::chrono::milliseconds timeout) {
  auto constexpr ping = "PING"sv;

  auto messages = std::make_shared<message_queue_t::element_type>(30);
  ref->message_queue_queue->raise(type, peer.address(), messages);

  auto fg = util::fail_guard([&]() {
    messages->stop();

    // remove message queue from session
    ref->message_queue_queue->raise(type, peer.address(), nullptr);
  });

  auto start_time   = std::chrono::steady_clock::now();
  auto current_time = start_time;

  while(current_time - start_time < config::stream.ping_timeout) {
    auto delta_time = current_time - start_time;

    auto msg_opt = messages->pop(config::stream.ping_timeout - delta_time);
    if(!msg_opt) {
      break;
    }

    TUPLE_2D_REF(port, msg, *msg_opt);
    if(msg == ping) {
      BOOST_LOG(debug) << "Received ping from "sv << peer.address() << ':' << port << " ["sv << util::hex_vec(msg) << ']';

      // Update connection details.
      {
        auto addr_str = peer.address().to_string();

        auto &connections = ref->audio_video_connections;

        auto lg = connections.lock();

        std::remove_reference_t<decltype(*connections)>::iterator pos = std::end(*connections);

        for(auto it = std::begin(*connections); it != std::end(*connections); ++it) {
          TUPLE_2D_REF(addr, port_ref, *it);

          if(!port_ref && addr_str == addr) {
            pos = it;
          }
          else if(port_ref == port) {
            break;
          }
        }

        if(pos == std::end(*connections)) {
          continue;
        }

        pos->second = port;
        peer.port(port);
      }

      return port;
    }

    BOOST_LOG(debug) << "Received non-ping from "sv << peer.address() << ':' << port << " ["sv << util::hex_vec(msg) << ']';

    current_time = std::chrono::steady_clock::now();
  }

  BOOST_LOG(error) << "Initial Ping Timeout"sv;
  return -1;
}

void videoThread(session_t *session) {
  auto fg = util::fail_guard([&]() {
    session::stop(*session);
  });

  while_starting_do_nothing(session->state);

  auto ref  = broadcast.ref();
  auto port = recv_ping(ref, socket_e::video, session->video.peer, config::stream.ping_timeout);
  if(port < 0) {
    return;
  }

  // Enable QoS tagging on video traffic if requested by the client
  if(session->config.videoQosType) {
    auto address       = session->video.peer.address();
    session->video.qos = std::move(platf::enable_socket_qos(ref->video_sock.native_handle(), address,
      session->video.peer.port(), platf::qos_data_type_e::video));
  }

  BOOST_LOG(debug) << "Start capturing Video"sv;
  video::capture(session->mail, session->config.monitor, session);
}

void audioThread(session_t *session) {
  auto fg = util::fail_guard([&]() {
    session::stop(*session);
  });

  while_starting_do_nothing(session->state);

  auto ref  = broadcast.ref();
  auto port = recv_ping(ref, socket_e::audio, session->audio.peer, config::stream.ping_timeout);
  if(port < 0) {
    return;
  }

  // Enable QoS tagging on audio traffic if requested by the client
  if(session->config.audioQosType) {
    auto address       = session->audio.peer.address();
    session->audio.qos = std::move(platf::enable_socket_qos(ref->audio_sock.native_handle(), address,
      session->audio.peer.port(), platf::qos_data_type_e::audio));
  }

  BOOST_LOG(debug) << "Start capturing Audio"sv;
  audio::capture(session->mail, session->config.audio, session);
}

namespace session {
std::atomic_uint running_sessions;

state_e state(session_t &session) {
  return session.state.load(std::memory_order_relaxed);
}

void stop(session_t &session) {
  while_starting_do_nothing(session.state);
  auto expected         = state_e::RUNNING;
  auto already_stopping = !session.state.compare_exchange_strong(expected, state_e::STOPPING);
  if(already_stopping) {
    return;
  }

  session.shutdown_event->raise(true);
}

void join(session_t &session) {
  // Current Nvidia drivers have a bug where NVENC can deadlock the encoder thread with hardware-accelerated
  // GPU scheduling enabled. If this happens, we will terminate ourselves and the service can restart.
  // The alternative is that Sunshine can never start another session until it's manually restarted.
  auto task = []() {
    BOOST_LOG(fatal) << "Hang detected! Session failed to terminate in 10 seconds."sv;
    log_flush();
    std::abort();
  };
  auto force_kill = task_pool.pushDelayed(task, 10s).task_id;
  auto fg         = util::fail_guard([&force_kill]() {
    // Cancel the kill task if we manage to return from this function
    task_pool.cancel(force_kill);
  });

  BOOST_LOG(debug) << "Waiting for video to end..."sv;
  session.videoThread.join();
  BOOST_LOG(debug) << "Waiting for audio to end..."sv;
  session.audioThread.join();
  BOOST_LOG(debug) << "Waiting for control to end..."sv;
  session.controlEnd.view();
  // Reset input on session stop to avoid stuck repeated keys
  BOOST_LOG(debug) << "Resetting Input..."sv;
  input::reset(session.input);

  BOOST_LOG(debug) << "Removing references to any connections..."sv;
  {
    auto video_addr = session.video.peer.address().to_string();
    auto audio_addr = session.audio.peer.address().to_string();

    auto video_port = session.video.peer.port();
    auto audio_port = session.audio.peer.port();

    auto &connections = session.broadcast_ref->audio_video_connections;

    auto lg = connections.lock();

    auto validate_size = connections->size();
    for(auto it = std::begin(*connections); it != std::end(*connections);) {
      TUPLE_2D_REF(addr, port, *it);

      if((video_port == port && video_addr == addr) ||
         (audio_port == port && audio_addr == addr)) {
        it = connections->erase(it);
      }
      else {
        ++it;
      }
    }

    auto new_size = connections->size();
    if(validate_size != new_size + 2) {
      BOOST_LOG(warning) << "Couldn't remove reference to session connections: ending all broadcasts"sv;

      // A reference to the event object is still stored somewhere else. So no need to keep
      // a reference to it.
      mail::man->event<bool>(mail::broadcast_shutdown)->raise(true);
    }
  }

  // If this is the last session, invoke the platform callbacks
  if(--running_sessions == 0) {
    platf::streaming_will_stop();
  }

  BOOST_LOG(debug) << "Session ended"sv;
}

int start(session_t &session, const std::string &addr_string) {
  session.input = input::alloc(session.mail);

  session.broadcast_ref = broadcast.ref();
  if(!session.broadcast_ref) {
    return -1;
  }

  session.broadcast_ref->control_server.emplace_addr_to_session(addr_string, session);

  auto addr = boost::asio::ip::make_address(addr_string);
  session.video.peer.address(addr);
  session.video.peer.port(0);

  session.audio.peer.address(addr);
  session.audio.peer.port(0);

  {
    auto &connections = session.broadcast_ref->audio_video_connections;

    auto lg = connections.lock();

    // allocate a location for connections
    connections->emplace_back(addr_string, 0);
    connections->emplace_back(addr_string, 0);
  }


  session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

  session.audioThread = std::thread { audioThread, &session };
  session.videoThread = std::thread { videoThread, &session };

  session.state.store(state_e::RUNNING, std::memory_order_relaxed);

  // If this is the first session, invoke the platform callbacks
  if(++running_sessions == 1) {
    platf::streaming_will_start();
  }

  return 0;
}

std::shared_ptr<session_t> alloc(config_t &config, crypto::aes_t &gcm_key, crypto::aes_t &iv) {
  auto session = std::make_shared<session_t>();

  auto mail = std::make_shared<safe::mail_raw_t>();

  session->shutdown_event = mail->event<bool>(mail::shutdown);

  session->config = config;

  session->control.rumble_queue = mail->queue<platf::rumble_t>(mail::rumble);
  session->control.hdr_queue    = mail->event<video::hdr_info_t>(mail::hdr);
  session->control.iv           = iv;
  session->control.cipher       = crypto::cipher::gcm_t {
    gcm_key, false
  };

  session->video.idr_events = mail->event<bool>(mail::idr);
  session->video.lowseq     = 0;

  constexpr auto max_block_size = crypto::cipher::round_to_pkcs7_padded(2048);

  util::buffer_t<char> shards { RTPA_TOTAL_SHARDS * max_block_size };
  util::buffer_t<uint8_t *> shards_p { RTPA_TOTAL_SHARDS };

  for(auto x = 0; x < RTPA_TOTAL_SHARDS; ++x) {
    shards_p[x] = (uint8_t *)&shards[x * max_block_size];
  }

  // Audio FEC spans multiple audio packets,
  // therefore its session specific
  session->audio.shards   = std::move(shards);
  session->audio.shards_p = std::move(shards_p);

  session->audio.fec_packet.reset((audio_fec_packet_raw_t *)malloc(sizeof(audio_fec_packet_raw_t) + max_block_size));

  session->audio.fec_packet->rtp.header     = 0x80;
  session->audio.fec_packet->rtp.packetType = 127;
  session->audio.fec_packet->rtp.timestamp  = 0;
  session->audio.fec_packet->rtp.ssrc       = 0;

  session->audio.fec_packet->fecHeader.payloadType = 97;
  session->audio.fec_packet->fecHeader.ssrc        = 0;

  session->audio.cipher = crypto::cipher::cbc_t {
    gcm_key, true
  };

  session->audio.avRiKeyId      = util::endian::big(*(std::uint32_t *)iv.data());
  session->audio.sequenceNumber = 0;
  session->audio.timestamp      = 0;

  session->control.peer = nullptr;
  session->state.store(state_e::STOPPED, std::memory_order_relaxed);

  session->mail = std::move(mail);

  return session;
}
} // namespace session
} // namespace stream
