//
// Created by loki on 6/5/19.
//
#include <boost/version.hpp>
#if ((BOOST_VERSION / 1000) >= 107)
#define EXECUTOR(x) (x->get_executor())
#else
#define EXECUTOR(x) (x->get_io_service())
#endif


#include <queue>
#include <iostream>
#include <future>
#include <boost/asio.hpp>
#include <moonlight-common-c/enet/include/enet/enet.h>
#include <fstream>
#include <openssl/err.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <moonlight-common-c/src/Video.h>
#include <moonlight-common-c/src/Rtsp.h>
#include <rs.h>
}

#include "config.h"
#include "utility.h"
#include "stream.h"
#include "audio.h"
#include "video.h"
#include "thread_safe.h"
#include "crypto.h"
#include "input.h"
#include "process.h"

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

namespace asio = boost::asio;
namespace sys  = boost::system;

using asio::ip::tcp;
using asio::ip::udp;

using namespace std::literals;

namespace stream {

constexpr auto RTSP_SETUP_PORT = 48010;
constexpr auto VIDEO_STREAM_PORT = 47998;
constexpr auto CONTROL_PORT = 47999;
constexpr auto AUDIO_STREAM_PORT = 48000;

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

safe::event_t<launch_session_t> launch_event;

struct config_t {
  audio::config_t audio;
  video::config_t monitor;
  int packetsize;

  bool sops;
  std::optional<int> gcmap;
};

struct session_t {
  config_t config;

  std::thread audioThread;
  std::thread videoThread;
  std::thread controlThread;

  std::chrono::steady_clock::time_point pingTimeout;

  video::packet_queue_t video_packets;
  audio::packet_queue_t audio_packets;

  crypto::aes_t gcm_key;
  crypto::aes_t iv;

  std::string app_name;
} session;

void free_host(ENetHost *host) {
  std::for_each(host->peers, host->peers + host->peerCount, [](ENetPeer &peer_ref) {
    ENetPeer *peer = &peer_ref;

    if(peer) {
      enet_peer_disconnect_now(peer, 0);
    }
  });

  enet_host_destroy(host);
}

void free_msg(PRTSP_MESSAGE msg) {
  freeMessage(msg);

  delete msg;
}

using msg_t          = util::safe_ptr<RTSP_MESSAGE, free_msg>;
using packet_t       = util::safe_ptr<ENetPacket, enet_packet_destroy>;
using host_t         = util::safe_ptr<ENetHost, free_host>;
using peer_t         = ENetPeer*;
using rh_t           = util::safe_ptr<reed_solomon, reed_solomon_release>;
using video_packet_t = util::safe_ptr<video_packet_raw_t, util::c_free>;
using audio_packet_t = util::safe_ptr<audio_packet_raw_t, util::c_free>;

host_t host_create(ENetAddress &addr, std::uint16_t port) {
  enet_address_set_host(&addr, "0.0.0.0");
  enet_address_set_port(&addr, port);

  return host_t { enet_host_create(PF_INET, &addr, 1, 1, 0, 0) };
}

void print_msg(PRTSP_MESSAGE msg);
void cmd_not_found(host_t &host, peer_t peer, msg_t&& req);

class rtsp_server_t {
public:
  rtsp_server_t(rtsp_server_t &&) noexcept = default;
  rtsp_server_t &operator=(rtsp_server_t &&) noexcept = default;

  explicit rtsp_server_t(std::uint16_t port) : _host { host_create(_addr, port) } {}

  template<class T, class X>
  void iterate(std::chrono::duration<T, X> timeout) {
    ENetEvent event;
    auto res = enet_host_service(_host.get(), &event, std::chrono::floor<std::chrono::milliseconds>(timeout).count());

    if(res > 0) {
      switch(event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
        {
          packet_t packet { event.packet };
	        peer_t peer { event.peer };

          msg_t req { new RTSP_MESSAGE {} };

	        //TODO: compare addresses of the peers
	        if(_queue_packet.second == nullptr) {
	          parseRtspMessage(req.get(), (char*)packet->data, packet->dataLength);
	          for(auto option = req->options; option != nullptr; option = option->next) {
	            if("Content-length"sv == option->option) {
	            _queue_packet = std::make_pair(peer, std::move(packet));
	            return;
	            }
	          }
	        }
	        else {
	          std::vector<char> full_payload;

	          auto old_msg = std::move(_queue_packet);
	          TUPLE_2D_REF(_, old_packet, old_msg);

	          std::string_view new_payload { (char*)packet->data, packet->dataLength };
	          std::string_view old_payload { (char*)old_packet->data, old_packet->dataLength };
	          full_payload.resize(new_payload.size() + old_payload.size());

	          std::copy(std::begin(old_payload), std::end(old_payload), std::begin(full_payload));
	          std::copy(std::begin(new_payload), std::end(new_payload), std::begin(full_payload) + old_payload.size());

            parseRtspMessage(req.get(), full_payload.data(), full_payload.size());
	        }

          print_msg(req.get());

	        msg_t resp;
	        auto func = _map_cmd_cb.find(req->message.request.command);
	        if(func != std::end(_map_cmd_cb)) {
            func->second(_host, peer, std::move(req));
	        }
	        else {
            cmd_not_found(_host, peer, std::move(req));
	        }

	        return;
        }
          break;
        case ENET_EVENT_TYPE_CONNECT:
          std::cout << "CLIENT CONNECTED TO RTSP" << std::endl;
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          std::cout << "CLIENT DISCONNECTED FROM RTSP" << std::endl;
          break;
        case ENET_EVENT_TYPE_NONE:
          break;
      }
    }
  }

  void map(const std::string_view &type, std::function<void(host_t &, peer_t, msg_t&&)> cb);
private:
  void _respond(peer_t &peer, msg_t &msg);

  // named _queue_packet because I want to make it an actual queue
  // It's like this for convenience sake
  std::pair<peer_t, packet_t> _queue_packet;

  std::unordered_map<std::string_view, std::function<void(host_t&, peer_t, msg_t&&)>> _map_cmd_cb;
  ENetAddress _addr;
  host_t _host;
};

class control_server_t {
public:
  control_server_t(control_server_t &&) noexcept = default;
  control_server_t &operator=(control_server_t &&) noexcept = default;

  explicit control_server_t(std::uint16_t port) : _host { host_create(_addr, port) } {}

  template<class T, class X>
  void iterate(std::chrono::duration<T, X> timeout) {
    ENetEvent event;
    auto res = enet_host_service(_host.get(), &event, std::chrono::floor<std::chrono::milliseconds>(timeout).count());

    if(res > 0) {
      switch(event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
        {
          packet_t packet { event.packet };

          std::uint16_t *type = (std::uint16_t *)packet->data;
          std::string_view payload { (char*)packet->data + sizeof(*type), packet->dataLength - sizeof(*type) };


          auto cb = _map_type_cb.find(*type);
          if(cb == std::end(_map_type_cb)) {
            std::cout << "type [Unknown] { " << util::hex(*type).to_string_view() << " }" << std::endl;
            std::cout << "---data---" << std::endl << util::hex_vec(payload) << std::endl << "---end data---" << std::endl;
          }

          else {
            cb->second(payload);
          }
        }
          break;
        case ENET_EVENT_TYPE_CONNECT:
          std::cout << "CLIENT CONNECTED" << std::endl;
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          std::cout << "CLIENT DISCONNECTED" << std::endl;
          break;
        case ENET_EVENT_TYPE_NONE:
          break;
      }
    }
  }
  void map(uint16_t type, std::function<void(const std::string_view&)> cb);
  void send(const std::string_view &payload);
private:
  std::unordered_map<std::uint16_t, std::function<void(const std::string_view&)>> _map_type_cb;
  ENetAddress _addr;
  host_t _host;
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
    std::cout << "Error: number of fragments for reed solomon exceeds DATA_SHARDS_MAX"sv << std::endl;
    std::cout << nr_shards << " > "sv << DATA_SHARDS_MAX << std::endl;

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

void print_msg(PRTSP_MESSAGE msg) {
  std::string_view type = msg->type == TYPE_RESPONSE ? "RESPONSE"sv : "REQUEST"sv;

  std::string_view payload { msg->payload, (size_t)msg->payloadLength };
  std::string_view protocol { msg->protocol };
  auto seqnm = msg->sequenceNumber;
  std::string_view messageBuffer { msg->messageBuffer };

  std::cout << "type ["sv << type << ']' << std::endl;
  std::cout << "sequence number ["sv << seqnm << ']' << std::endl;
  std::cout << "protocol :: "sv << protocol << std::endl;
  std::cout << "payload :: "sv << payload << std::endl;

  if(msg->type == TYPE_RESPONSE) {
    auto &resp = msg->message.response;

    auto statuscode = resp.statusCode;
    std::string_view status { resp.statusString };

    std::cout << "statuscode :: "sv << statuscode << std::endl;
    std::cout << "status :: "sv << status << std::endl;
  }
  else {
    auto& req = msg->message.request;

    std::string_view command { req.command };
    std::string_view target { req.target };

    std::cout << "command :: "sv << command << std::endl;
    std::cout << "target :: "sv << target << std::endl;
  }

  for(auto option = msg->options; option != nullptr; option = option->next) {
    std::string_view content { option->content };
    std::string_view name { option->option };

    std::cout << name << " :: "sv << content << std::endl;
  }

  std::cout << "---Begin MessageBuffer---"sv << std::endl << messageBuffer << std::endl << "---End MessageBuffer---"sv << std::endl << std::endl;
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

void rtsp_server_t::map(const std::string_view& cmd, std::function<void(host_t&, peer_t, msg_t&&)> cb) {
  _map_cmd_cb.emplace(cmd, std::move(cb));
}

void control_server_t::map(uint16_t type, std::function<void(const std::string_view &)> cb) {
  _map_type_cb.emplace(type, std::move(cb));
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

void controlThread(video::idr_event_t idr_events) {
  control_server_t server { CONTROL_PORT };

  auto input = platf::input();
  server.map(packetTypes[IDX_START_A], [](const std::string_view &payload) {
    session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    std::cout << "type [IDX_START_A]"sv << std::endl;
  });

  server.map(packetTypes[IDX_START_B], [](const std::string_view &payload) {
    session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    std::cout << "type [IDX_START_B]"sv << std::endl;
  });

  server.map(packetTypes[IDX_LOSS_STATS], [](const std::string_view &payload) {
    session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    std::cout << "type [IDX_LOSS_STATS]"sv << std::endl;

    int32_t *stats = (int32_t*)payload.data();
    auto count = stats[0];
    std::chrono::milliseconds t { stats[1] };

    auto lastGoodFrame = stats[3];

    std::cout << "---begin stats---" << std::endl;
    std::cout << "loss count since last report [" << count << ']' << std::endl;
    std::cout << "time in milli since last report [" << t.count() << ']' << std::endl;
    std::cout << "last good frame [" << lastGoodFrame << ']' << std::endl;
    std::cout << "---end stats---" << std::endl;
  });

  server.map(packetTypes[IDX_INVALIDATE_REF_FRAMES], [idr_events](const std::string_view &payload) {
    session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    std::cout << "type [IDX_INVALIDATE_REF_FRAMES]"sv << std::endl;

    std::int64_t *frames = (std::int64_t *)payload.data();
    auto firstFrame = frames[0];
    auto lastFrame  = frames[1];

    std::cout << "firstFrame [" << firstFrame << ']' << std::endl;
    std::cout << "lastFrame [" << lastFrame << ']' << std::endl;

    idr_events->raise(std::make_pair(firstFrame, lastFrame));
  });

  server.map(packetTypes[IDX_INPUT_DATA], [&input](const std::string_view &payload) mutable {
    session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    std::cout << "type [IDX_INPUT_DATA]"sv << std::endl;

    int32_t tagged_cipher_length = util::endian::big(*(int32_t*)payload.data());
    std::string_view tagged_cipher { payload.data() + sizeof(tagged_cipher_length), (size_t)tagged_cipher_length };

    crypto::cipher_t cipher { session.gcm_key };
    cipher.padding = false;

    std::vector<uint8_t> plaintext;
    if(cipher.decrypt_gcm(session.iv, tagged_cipher, plaintext)) {
      // something went wrong :(

      std::cout << "failed to verify tag"sv << std::endl;
      session.video_packets->stop();
      session.audio_packets->stop();
    }

    if(tagged_cipher_length >= 16 + session.iv.size()) {
      std::copy(payload.end() - 16, payload.end(), std::begin(session.iv));
    }

    input::print(plaintext.data());
    input::passthrough(input, plaintext.data());
  });

  while(session.video_packets->running()) {
    if(std::chrono::steady_clock::now() > session.pingTimeout) {
      std::cout << "ping timeout"sv << std::endl;
      session.video_packets->stop();
      session.audio_packets->stop();
    }

    if(!session.app_name.empty() && !proc::proc.running()) {
      std::cout << "Process terminated"sv << std::endl;

      std::uint16_t reason = 0x0100;

      std::array<std::uint16_t, 2> payload;
      payload[0] = packetTypes[IDX_TERMINATION];
      payload[1] = reason;

      server.send(std::string_view {(char*)payload.data(), payload.size()});

      session.video_packets->stop();
      session.audio_packets->stop();
    }

    server.iterate(500ms);
  }
}

template<class Stream, class Peer, class BufferSequence>
util::Either<std::size_t, sys::error_code> asio_read(Stream &s, asio::io_service &io, const BufferSequence &bufs, Peer &peer, const asio::deadline_timer::duration_type& expire_time) {
  std::optional<sys::error_code> timer_result, read_result;

  asio::deadline_timer timer { io };

  timer.expires_from_now(boost::posix_time::milliseconds(config::stream.ping_timeout.count()));
  timer.async_wait([&](sys::error_code c){
    timer_result = c;
  });

  std::size_t len = 0;
  s.async_receive_from(bufs, peer, 0, [&](const boost::system::error_code &ec, size_t bytes) {
    len = bytes;

    read_result = ec;
  });

  io.reset();

  while(io.run_one()) {
    if(read_result) {
      timer.cancel();
    }
    else if(timer_result) {
      s.cancel();
    }
  }

  if(*read_result) {
    return *read_result;
  }

  return len;
}

template<class T>
std::optional<udp::endpoint> recv_peer(std::shared_ptr<safe::queue_t<T>> &queue, udp::socket &sock, asio::io_service &io) {
  std::array<char, 2048> buf;

  char ping[] = {
    0x50, 0x49, 0x4E, 0x47
  };

  udp::endpoint peer;

  while (queue->running()) {
    auto len_or_err = asio_read(sock, io, asio::buffer(buf), peer, boost::posix_time::milliseconds(config::stream.ping_timeout.count()));

    if(len_or_err.has_right() || len_or_err.left() == 0) {
      return std::nullopt;
    }

    auto len = len_or_err.left();
    if (len == 4 && !std::memcmp(ping, buf.data(), sizeof(ping))) {
      std::cout << "PING from ["sv << peer.address().to_string() << ':' << peer.port() << ']' << std::endl;

      return std::make_optional(std::move(peer));
    }

    std::cout << "Unknown transmission: "sv << util::hex_vec(std::string_view{buf.data(), len}) << std::endl;
  }

  return std::nullopt;
}

void audioThread() {
  auto &config = session.config;

  asio::io_service io;
  udp::socket sock{io, udp::endpoint(udp::v6(), AUDIO_STREAM_PORT)};

  auto peer = recv_peer(session.audio_packets, sock, io);
  if(!peer) {
    return;
  }

  auto &packets = session.audio_packets;
  std::thread captureThread{audio::capture, packets, config.audio};

  uint16_t frame{1};

  while (auto packet = packets->pop()) {
    audio_packet_t audio_packet { (audio_packet_raw_t*)malloc(sizeof(audio_packet_raw_t) + packet->size()) };

    audio_packet->rtp.sequenceNumber = util::endian::big(frame++);
    audio_packet->rtp.packetType = 97;
    std::copy(std::begin(*packet), std::end(*packet), audio_packet->payload());

    sock.send_to(asio::buffer((char*)audio_packet.get(), sizeof(audio_packet_raw_t) + packet->size()), *peer);
    // std::cout << "Audio ["sv << frame << "] ::  send..."sv << std::endl;
  }

  std::cout << "Audio: Joining()" << std::endl;
  captureThread.join();
  std::cout << "Audio: Joining()" << std::endl;
}

void videoThread(video::idr_event_t idr_events) {
  auto &config = session.config;

  int lowseq = 0;
  
  asio::io_service io;
  udp::socket sock{io, udp::endpoint(udp::v6(), VIDEO_STREAM_PORT)};

  auto peer = recv_peer(session.video_packets, sock, io);
  if(!peer) {
    return;
  }

  auto &packets = session.video_packets;
  std::thread captureThread{video::capture_display, packets, idr_events, config.monitor};

  while (auto packet = packets->pop()) {
    std::string_view payload{(char *) packet->data, (size_t) packet->size};
    std::vector<uint8_t> payload_new;

    auto nv_packet_header = "\0017charss"sv;
    std::copy(std::begin(nv_packet_header), std::end(nv_packet_header), std::back_inserter(payload_new));
    std::copy(std::begin(payload), std::end(payload), std::back_inserter(payload_new));

    payload = {(char *) payload_new.data(), payload_new.size()};

    // make sure moonlight recognizes the nalu code for IDR frames
    if (packet->flags & AV_PKT_FLAG_KEY) {
      //TODO: Not all encoders encode their IDR frames with `"\000\000\001e"`
      auto seq_i_frame_old = "\000\000\001e"sv;
      auto seq_i_frame = "\000\000\000\001e"sv;

      assert(std::search(std::begin(payload), std::end(payload), std::begin(seq_i_frame), std::end(seq_i_frame)) ==
             std::end(payload));
      payload_new = replace(payload, seq_i_frame_old, seq_i_frame);

      payload = {(char *) payload_new.data(), payload_new.size()};
    }

    // insert packet headers
    auto blocksize = config.packetsize + MAX_RTP_HEADER_SIZE;
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
      std::cout << "skipping frame..."sv << std::endl;
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
      sock.send_to(asio::buffer(shards[x]), *peer);
    }

    if(packet->flags & AV_PKT_FLAG_KEY) {
      std::cout << "Key "sv;
    }

    std::cout << "Frame ["sv << packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv << std::endl;
    lowseq += shards.size();

  }
  std::cout << "Video: Joining()" << std::endl;
  captureThread.join();
  std::cout << "Video: Joined()" << std::endl;
}

void respond(host_t &host, peer_t peer, msg_t &resp) {
  auto payload = std::make_pair(resp->payload, resp->payloadLength);

  auto lg = util::fail_guard([&]() {
    resp->payload = payload.first;
    resp->payloadLength = payload.second;
  });

  resp->payload = nullptr;
  resp->payloadLength = 0;

  int serialized_len;
  util::c_ptr<char> raw_resp { serializeRtspMessage(resp.get(), &serialized_len) };
  std::cout << "---Begin Response---"sv << std::endl
	    << std::string_view { raw_resp.get(), (std::size_t)serialized_len } << std::endl
	    << std::string_view { payload.first, (std::size_t)payload.second } << std::endl
	    << "---End Response---"sv << std::endl 
	    << std::endl;

  std::string_view tmp_resp { raw_resp.get(), (size_t)serialized_len };

  {
    auto packet = enet_packet_create(tmp_resp.data(), tmp_resp.size(), ENET_PACKET_FLAG_RELIABLE);
    if(enet_peer_send(peer, 0, packet)) {
      enet_packet_destroy(packet);
      return;
    }

    enet_host_flush(host.get());
  }

  if(payload.second > 0) {
    auto packet = enet_packet_create(payload.first, payload.second, ENET_PACKET_FLAG_RELIABLE);;
    if(enet_peer_send(peer, 0, packet)) {
      enet_packet_destroy(packet);
      return;
    }

    enet_host_flush(host.get());
  }
}

void respond(host_t &host, peer_t peer, POPTION_ITEM options, int statuscode, const char *status_msg, int seqn, const std::string_view &payload) {
  msg_t resp { new msg_t::element_type };
  createRtspResponse(resp.get(), nullptr, 0, const_cast<char*>("RTSP/1.0"), statuscode, const_cast<char*>(status_msg), seqn, options, const_cast<char*>(payload.data()), (int)payload.size());

  respond(host, peer, resp);
}

void cmd_not_found(host_t &host, peer_t peer, msg_t&& req) {
  respond(host, peer, nullptr, 404, "NOT FOUND", req->sequenceNumber, {});
}

void cmd_option(host_t &host, peer_t peer, msg_t&& req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  respond(host, peer, &option, 200, "OK", req->sequenceNumber, {});
}

void cmd_describe(host_t &host, peer_t peer, msg_t&& req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  // FIXME: Moonlight will accept the payload, but the value of the option is not correct
  respond(host, peer, &option, 200, "OK", req->sequenceNumber, "surround-params=NONE"sv);
}

void cmd_setup(host_t &host, peer_t peer, msg_t &&req) {
  OPTION_ITEM options[2] {};

  auto &seqn           = options[0];
  auto &session_option = options[1];

  seqn.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  seqn.content = const_cast<char*>(seqn_str.c_str());

  if(session.video_packets) {
    // already streaming

    respond(host, peer, &seqn, 503, "Service Unavailable", req->sequenceNumber, {});
    return;
  }

  std::string_view target { req->message.request.target };
  auto begin = std::find(std::begin(target), std::end(target), '=') + 1;
  auto end   = std::find(begin, std::end(target), '/');
  std::string_view type { begin, (size_t)std::distance(begin, end) };

  if(type == "audio"sv) {
    seqn.next = &session_option;

    session_option.option  = const_cast<char*>("Session");
    session_option.content = const_cast<char*>("DEADBEEFCAFE;timeout = 90");
  }
  else if(type != "video"sv && type != "control"sv) {
    cmd_not_found(host, peer, std::move(req));

    return;
  }

  respond(host, peer, &seqn, 200, "OK", req->sequenceNumber, {});
}

void cmd_announce(host_t &host, peer_t peer, msg_t &&req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  if(session.video_packets || !launch_event.peek()) {
    //Either already streaming or /launch has not been used

    respond(host, peer, &option, 503, "Service Unavailable", req->sequenceNumber, {});
    return;
  }
  auto launch_session { std::move(*launch_event.pop()) };

  std::string_view payload { req->payload, (size_t)req->payloadLength };

  std::vector<std::string_view> lines;

  auto whitespace = [](char ch) {
    return ch == '\n' || ch == '\r';
  };

  {
    auto pos = std::begin(payload);
    auto begin = pos;
    while (pos != std::end(payload)) {
      if (whitespace(*pos++)) {
        lines.emplace_back(begin, pos - begin - 1);

        while(whitespace(*pos)) { ++pos; }
        begin = pos;
      }
    }
  }

  std::string_view client;
  std::unordered_map<std::string_view, std::string_view> args;

  for(auto line : lines) {
    auto type = line.substr(0, 2);
    if(type == "s="sv) {
      client = line.substr(2);
    }
    else if(type == "a=") {
      auto pos = line.find(':');

      auto name = line.substr(2, pos - 2);
      auto val = line.substr(pos + 1);

      if(val[val.size() -1] == ' ') {
        val = val.substr(0, val.size() -1);
      }
      args.emplace(name, val);
    }
  }

  try {

    auto &config = session.config;
    config.audio.channels       = util::from_view(args.at("x-nv-audio.surround.numChannels"sv));
    config.audio.mask           = util::from_view(args.at("x-nv-audio.surround.channelMask"sv));
    config.audio.packetDuration = util::from_view(args.at("x-nv-aqos.packetDuration"sv));

    config.packetsize = util::from_view(args.at("x-nv-video[0].packetSize"sv));

    config.monitor.height         = util::from_view(args.at("x-nv-video[0].clientViewportHt"sv));
    config.monitor.width          = util::from_view(args.at("x-nv-video[0].clientViewportWd"sv));
    config.monitor.framerate      = util::from_view(args.at("x-nv-video[0].maxFPS"sv));
    config.monitor.bitrate        = util::from_view(args.at("x-nv-vqos[0].bw.maximumBitrateKbps"sv));
    config.monitor.slicesPerFrame = util::from_view(args.at("x-nv-video[0].videoEncoderSlicesPerFrame"sv));

  } catch(std::out_of_range &) {

    respond(host, peer, &option, 400, "BAD REQUEST", req->sequenceNumber, {});
    return;
  }

  auto &app_name = launch_session.app_name;
  auto &gcm_key  = launch_session.gcm_key;
  auto &iv       = launch_session.iv;

  if(!app_name.empty() && session.app_name != app_name  ) {
    if(auto err_code = proc::proc.execute(app_name)) {
      if(err_code == 404) {
        respond(host, peer, &option, 404, (app_name + " NOT FOUND").c_str(), req->sequenceNumber, {});
        return;
      }

      else {
        respond(host, peer, &option, 500, "INTERNAL ERROR", req->sequenceNumber, {});
        return;
      }
    }
  }

  session.app_name = std::move(app_name);

  std::copy(std::begin(gcm_key), std::end(gcm_key), std::begin(session.gcm_key));
  std::copy(std::begin(iv), std::end(iv), std::begin(session.iv));

  session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

  session.video_packets = std::make_shared<video::packet_queue_t::element_type>();
  session.audio_packets = std::make_shared<audio::packet_queue_t::element_type>();

  video::idr_event_t idr_events {new video::idr_event_t::element_type };
  session.audioThread   = std::thread {audioThread};
  session.videoThread   = std::thread {videoThread, idr_events};
  session.controlThread = std::thread {controlThread, idr_events};

  respond(host, peer, &option, 200, "OK", req->sequenceNumber, {});
}

void cmd_play(host_t &host, peer_t peer, msg_t &&req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  respond(host, peer, &option, 200, "OK", req->sequenceNumber, {});
}

void rtpThread() {
  rtsp_server_t server(RTSP_SETUP_PORT);

  server.map("OPTIONS"sv, &cmd_option);
  server.map("DESCRIBE"sv, &cmd_describe);
  server.map("SETUP"sv, &cmd_setup);
  server.map("ANNOUNCE"sv, &cmd_announce);

  server.map("PLAY"sv, &cmd_play);

  while(true) {
    server.iterate(config::stream.ping_timeout);

    if(session.video_packets && !session.video_packets->running()) {
      std::cout << "Waiting for Audio to end..."sv << std::endl;
      session.audioThread.join();
      std::cout << "Waiting for Video to end..."sv << std::endl;
      session.videoThread.join();
      std::cout << "Waiting for Control to end..."sv << std::endl;
      session.controlThread.join();

      std::cout << "Resetting Session..."sv << std::endl;
      session.video_packets = video::packet_queue_t();
      session.audio_packets = audio::packet_queue_t();
    }
  }
}

}
