//
// Created by loki on 6/5/19.
//

#include <queue>
#include <iostream>
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
#include "queue.h"
#include "crypto.h"
#include "input.h"

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

crypto::aes_t gcm_key;
crypto::aes_t iv;

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
  int client_state;

  crypto::aes_t gcm_key;
  crypto::aes_t iv;
} session;

void free_msg(PRTSP_MESSAGE msg) {
  freeMessage(msg);

  delete msg;
}

using msg_t          = util::safe_ptr<RTSP_MESSAGE, free_msg>;
using packet_t       = util::safe_ptr<ENetPacket, enet_packet_destroy>;
using host_t         = util::safe_ptr<ENetHost, enet_host_destroy>;
using rh_t           = util::safe_ptr<reed_solomon, reed_solomon_release>;
using video_packet_t = util::safe_ptr<video_packet_raw_t, util::c_free>;
using audio_packet_t = util::safe_ptr<audio_packet_raw_t, util::c_free>;

host_t host_create(ENetAddress &addr, std::uint16_t port) {
  enet_address_set_host(&addr, "0.0.0.0");
  enet_address_set_port(&addr, port);

  return host_t { enet_host_create(PF_INET, &addr, 1, 1, 0, 0) };
}

class server_t {
public:
  server_t(server_t &&) noexcept = default;
  server_t &operator=(server_t &&) noexcept = default;

  explicit server_t(std::uint16_t port) : _host { host_create(_addr, port) } {}

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
private:
  std::unordered_map<std::uint8_t, std::function<void(const std::string_view&)>> _map_type_cb;
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
    std::cerr << "Error: number of fragments for reed solomon exceeds DATA_SHARDS_MAX"sv << std::endl;
    std::cerr << nr_shards << " > "sv << DATA_SHARDS_MAX << std::endl;
    exit(9);
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

  if(pad) {
    auto x = elements - 1;
    void *p = &result[x*(insert_size + slice_size)];

    f(p, x, elements);

    std::copy(next, std::end(data), (char*)p + insert_size);
  }

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

using frame_queue_t = std::vector<video::packet_t>;
video::packet_t next_packet(uint16_t &frame, std::shared_ptr<safe::queue_t<video::packet_t>> &packets, frame_queue_t &packet_queue) {
  auto packet = packets->pop();

  if(!packet) {
    return nullptr;
  }

  assert(packet->pts >= frame);

  auto comp = [](const video::packet_t &l, const video::packet_t &r) {
    return l->pts > r->pts;
  };

  if(packet->pts > frame) {
    packet_queue.emplace_back(std::move(packet));
    std::push_heap(std::begin(packet_queue), std::end(packet_queue), comp);

    if (packet_queue.front()->pts != frame) {
      return next_packet(frame, packets, packet_queue);
    }

    std::pop_heap(std::begin(packet_queue), std::end(packet_queue), comp);
    packet = std::move(packet_queue.back());
    packet_queue.pop_back();
  }

  ++frame;
  return packet;
}

std::vector<uint8_t> replace(const std::string_view &original, const std::string_view &old, const std::string_view &_new) {
  std::vector<uint8_t> replaced;

  auto search = [&](auto it) {
    return std::search(it, std::end(original), std::begin(old), std::end(old));
  };

  auto begin = std::begin(original);
  for(auto next = search(begin); next != std::end(original); next = search(++next)) {
    std::copy(begin, next, std::back_inserter(replaced));
    std::copy(std::begin(_new), std::end(_new), std::back_inserter(replaced));

    next = begin = next + old.size();
  }

  std::copy(begin, std::end(original), std::back_inserter(replaced));

  return replaced;
}

void server_t::map(uint16_t type, std::function<void(const std::string_view &)> cb) {
  _map_type_cb.emplace(type, std::move(cb));
}

void controlThread() {
  server_t server { CONTROL_PORT };

  std::shared_ptr display = platf::display();
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

/*    std::cout << "type [IDX_LOSS_STATS]"sv << std::endl;

    int32_t *stats = (int32_t*)payload.data();
    auto count = stats[0];
    std::chrono::milliseconds t { stats[1] };

    auto lastGoodFrame = stats[3];

    std::cout << "---begin stats---" << std::endl;
    std::cout << "loss count since last report [" << count << ']' << std::endl;
    std::cout << "time in milli since last report [" << t.count() << ']' << std::endl;
    std::cout << "last good frame [" << lastGoodFrame << ']' << std::endl;
    std::cout << "---end stats---" << std::endl; */
  });

  server.map(packetTypes[IDX_INVALIDATE_REF_FRAMES], [](const std::string_view &payload) {
    session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

    std::cout << "type [IDX_INVALIDATE_REF_FRAMES]"sv << std::endl;

    std::int64_t *frames = (std::int64_t *)payload.data();
    auto firstFrame = frames[0];
    auto lastFrame  = frames[1];

    std::cout << "firstFrame [" << firstFrame << ']' << std::endl;
    std::cout << "lastFrame [" << lastFrame << ']' << std::endl;
  });

  server.map(packetTypes[IDX_INPUT_DATA], [display](const std::string_view &payload) mutable {
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
      session.client_state = 0;
    }

    if(tagged_cipher_length >= 16 + session.iv.size()) {
      std::copy(payload.end() - 16, payload.end(), std::begin(session.iv));
    }

    input::print(plaintext.data());
    input::passthrough(display.get(), plaintext.data());
  });

  while(session.client_state > 0) {
    if(std::chrono::steady_clock::now() > session.pingTimeout) {
      session.client_state = 0;
    }

    server.iterate(2s);
  }
}

std::optional<udp::endpoint> recv_peer(udp::socket &sock) {
  std::array<char, 2048> buf;

  char ping[] = {
    0x50, 0x49, 0x4E, 0x47
  };

  udp::endpoint peer;
  while (session.client_state > 0) {
    asio::deadline_timer timer { sock.get_executor() };
    timer.expires_from_now(boost::posix_time::seconds(2));
    timer.async_wait([&](sys::error_code c){
      sock.cancel();
    });

    sys::error_code ping_error;
    auto len = sock.receive_from(asio::buffer(buf), peer, 0, ping_error);
    if(ping_error == sys::errc::make_error_code(sys::errc::operation_canceled)) {
      return {};
    }

    timer.cancel();

    if (len == 4 && !std::memcmp(ping, buf.data(), sizeof(ping))) {
      std::cout << "PING from ["sv << peer.address().to_string() << ':' << peer.port() << ']' << std::endl;

      return std::make_optional(std::move(peer));;
    }

    std::cout << "Unknown transmission: "sv << util::hex_vec(std::string_view{buf.data(), len}) << std::endl;
  }

  return {};
}

void audioThread() {
  auto &config = session.config;

  asio::io_service io;
  udp::socket sock{io, udp::endpoint(udp::v6(), AUDIO_STREAM_PORT)};

  auto peer = recv_peer(sock);
  if(!peer) {
    return;
  }

  std::shared_ptr<safe::queue_t<audio::packet_t>> packets{new safe::queue_t<audio::packet_t>};

  std::thread captureThread{audio::capture, packets, config.audio};

  uint16_t frame{1};

  while (auto packet = packets->pop()) {
    if(session.client_state == 0) {
      packets->stop();

      break;
    }

    audio_packet_t audio_packet { (audio_packet_raw_t*)malloc(sizeof(audio_packet_raw_t) + packet->size()) };

    audio_packet->rtp.sequenceNumber = util::endian::big(frame++);
    audio_packet->rtp.packetType = 97;
    std::copy(std::begin(*packet), std::end(*packet), audio_packet->payload());

    sock.send_to(asio::buffer((char*)audio_packet.get(), sizeof(audio_packet_raw_t) + packet->size()), *peer);
    // std::cout << "Audio ["sv << frame << "] ::  send..."sv << std::endl;
  }

  captureThread.join();
}

void videoThread() {
  auto &config = session.config;

  int lowseq = 0;
  
  asio::io_service io;
  udp::socket sock{io, udp::endpoint(udp::v6(), VIDEO_STREAM_PORT)};

  auto peer = recv_peer(sock);
  if(!peer) {
    return;
  }

  std::shared_ptr<safe::queue_t<video::packet_t>> packets{new safe::queue_t<video::packet_t>};

  std::thread captureThread{video::capture_display, packets, config.monitor};

  frame_queue_t packet_queue;
  uint16_t frame{1};

  while (auto packet = next_packet(frame, packets, packet_queue)) {
    if(session.client_state == 0) {
      packets->stop();

      break;
    }

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

    auto fecpercentage { 25 };

    payload_new = insert(sizeof(video_packet_raw_t), payload_blocksize,
                                              payload, [&](void *p, int fecIndex, int end) {
        video_packet_raw_t *video_packet = (video_packet_raw_t *)p;

        video_packet->packet.flags = FLAG_CONTAINS_PIC_DATA;
        video_packet->packet.frameIndex = packet->pts;
        video_packet->packet.streamPacketIndex = ((uint32_t)lowseq + fecIndex) << 8;
        video_packet->packet.fecInfo = (
          fecIndex << 12 |
          end << 22 |
          fecpercentage << 4
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

    auto shards = fec::encode(payload, blocksize, 25);

    for (auto x = shards.data_shards; x < shards.size(); ++x) {
      video_packet_raw_t *inspect = (video_packet_raw_t *)shards[x].data();

      inspect->packet.flags = FLAG_CONTAINS_PIC_DATA;
      inspect->packet.streamPacketIndex = ((uint32_t)(lowseq + x)) << 8;
      inspect->packet.frameIndex = packet->pts;
      inspect->packet.fecInfo = (
        x << 12 |
        shards.data_shards << 22 |
        fecpercentage << 4
      );

      inspect->rtp.sequenceNumber = util::endian::big<uint16_t>(lowseq + x);
    }

    for (auto x = 0; x < shards.size(); ++x) {
      sock.send_to(asio::buffer(shards[x]), *peer);
    }

    // std::cout << "Frame ["sv << packet->pts << "] :: send ["sv << shards.size() << "] shards..."sv << std::endl;
    lowseq += shards.size();

  }

  captureThread.join();
}

void respond(tcp::socket &sock, POPTION_ITEM options, int statuscode, const char *status_msg, int seqn, const std::string_view &payload) {
  RTSP_MESSAGE resp {};

  auto g = util::fail_guard([&]() {
    freeMessage(&resp);
  });

  createRtspResponse(&resp, nullptr, 0, const_cast<char*>("RTSP/1.0"), statuscode, const_cast<char*>(status_msg), seqn, options, const_cast<char*>(payload.data()), (int)payload.size());

  int serialized_len;
  util::c_ptr<char> raw_resp { serializeRtspMessage(&resp, &serialized_len) };

  std::string_view tmp_resp { raw_resp.get(), (size_t)serialized_len };
  std::cout << "---Begin Response---" << std::endl << tmp_resp << "---End Response---" << std::endl << std::endl;

  asio::write(sock, asio::buffer(tmp_resp));
}

void cmd_not_found(tcp::socket &&sock, msg_t&& req) {
  respond(sock, nullptr, 404, "NOT FOUND", req->sequenceNumber, {});
}

void cmd_option(tcp::socket &&sock, msg_t&& req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  respond(sock, &option, 200, "OK", req->sequenceNumber, {});
}

void cmd_describe(tcp::socket &&sock, msg_t&& req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  // FIXME: Moonlight will accept the payload, but the value of the option is not correct
  respond(sock, &option, 200, "OK", req->sequenceNumber, "surround-params=NONE"sv);
}

void cmd_setup(tcp::socket &&sock, msg_t &&req) {
  OPTION_ITEM options[2] {};

  auto &seqn           = options[0];
  auto &session_option = options[1];

  seqn.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  seqn.content = const_cast<char*>(seqn_str.c_str());

  if(session.client_state >= 0) {
    // already streaming

    respond(sock, &seqn, 503, "Service Unavailable", req->sequenceNumber, {});
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
    cmd_not_found(std::move(sock), std::move(req));

    return;
  }

  respond(sock, &seqn, 200, "OK", req->sequenceNumber, {});
}

void cmd_announce(tcp::socket &&sock, msg_t &&req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  if(session.client_state >= 0) {
    // already streaming

    respond(sock, &option, 503, "Service Unavailable", req->sequenceNumber, {});
    return;
  }

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

  auto &config = session.config;
  config.monitor.height         = util::from_view(args.at("x-nv-video[0].clientViewportHt"sv));
  config.monitor.width          = util::from_view(args.at("x-nv-video[0].clientViewportWd"sv));
  config.monitor.framerate      = util::from_view(args.at("x-nv-video[0].maxFPS"sv));
  config.monitor.bitrate        = util::from_view(args.at("x-nv-video[0].initialBitrateKbps"sv));
  config.monitor.slicesPerFrame = util::from_view(args.at("x-nv-video[0].videoEncoderSlicesPerFrame"sv));

  config.audio.channels       = util::from_view(args.at("x-nv-audio.surround.numChannels"sv));
  config.audio.mask           = util::from_view(args.at("x-nv-audio.surround.channelMask"sv));
  config.audio.packetDuration = util::from_view(args.at("x-nv-aqos.packetDuration"sv));

  config.packetsize = util::from_view(args.at("x-nv-video[0].packetSize"sv));

  std::copy(std::begin(gcm_key), std::end(gcm_key), std::begin(session.gcm_key));
  std::copy(std::begin(iv), std::end(iv), std::begin(session.iv));

  session.pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;
  session.client_state = 1;

  session.audioThread   = std::thread {audioThread};
  session.videoThread   = std::thread {videoThread};
  session.controlThread = std::thread {controlThread};

  respond(sock, &option, 200, "OK", req->sequenceNumber, {});
}

void cmd_play(tcp::socket &&sock, msg_t &&req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  respond(sock, &option, 200, "OK", req->sequenceNumber, {});
}

void rtpThread() {
  session.client_state = -1;

  asio::io_service io;

  tcp::acceptor acceptor { io, tcp::endpoint { tcp::v6(), RTSP_SETUP_PORT } };

  std::unordered_map<std::string_view, std::function<void(tcp::socket &&, msg_t &&)>> map_cmd_func;
  map_cmd_func.emplace("OPTIONS"sv, &cmd_option);
  map_cmd_func.emplace("DESCRIBE"sv, &cmd_describe);
  map_cmd_func.emplace("SETUP"sv, &cmd_setup);
  map_cmd_func.emplace("ANNOUNCE"sv, &cmd_announce);

  map_cmd_func.emplace("PLAY"sv, &cmd_play);

  while(true) {
    tcp::socket sock { io };

    acceptor.accept(sock);
    sock.set_option(tcp::no_delay(true));

    std::array<char, 2048> buf;

    auto len = sock.read_some(asio::buffer(buf));
    buf[std::min(buf.size(), len)] = '\0';

    msg_t req { new RTSP_MESSAGE {} };

    parseRtspMessage(req.get(), buf.data(), len);

    print_msg(req.get());

    auto func = map_cmd_func.find(req->message.request.command);
    if(func == std::end(map_cmd_func)) {
      cmd_not_found(std::move(sock), std::move(req));
    }
    else {
      func->second(std::move(sock), std::move(req));
    }

    if(session.client_state == 0) {
      session.audioThread.join();
      session.videoThread.join();
      session.controlThread.join();

      session.client_state = -1;
    }
  }
}

}
