//
// Created by loki on 2/2/20.
//

extern "C" {
#include <moonlight-common-c/src/Rtsp.h>
}

#include "config.h"
#include "main.h"
#include "network.h"
#include "rtsp.h"
#include "input.h"
#include "stream.h"

namespace asio = boost::asio;

using asio::ip::tcp;
using asio::ip::udp;

using namespace std::literals;

namespace stream {
constexpr auto RTSP_SETUP_PORT = 48010;

void free_msg(PRTSP_MESSAGE msg) {
  freeMessage(msg);

  delete msg;
}

class rtsp_server_t;

using msg_t = util::safe_ptr<RTSP_MESSAGE, free_msg>;
using cmd_func_t = std::function<void(rtsp_server_t *, const std::shared_ptr<session_t> &, net::peer_t, msg_t&&)>;

safe::event_t<launch_session_t> launch_event;

void print_msg(PRTSP_MESSAGE msg);
void cmd_not_found(net::host_t::pointer host, net::peer_t peer, msg_t&& req);

class rtsp_server_t {
public:
  rtsp_server_t(rtsp_server_t &&) noexcept = default;
  rtsp_server_t &operator=(rtsp_server_t &&) noexcept = default;

  explicit rtsp_server_t(std::uint16_t port) : _session_slots (config::stream.channels), _host {net::host_create(_addr, 1, port) } {}

  template<class T, class X>
  void iterate(std::chrono::duration<T, X> timeout) {
    ENetEvent event;
    auto res = enet_host_service(_host.get(), &event, std::chrono::floor<std::chrono::milliseconds>(timeout).count());

    if (res > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE: {
          net::packet_t packet{event.packet};
          net::peer_t peer{event.peer};

          msg_t req { new msg_t::element_type };

          //TODO: compare addresses of the peers
          if (_queue_packet.second == nullptr) {
            parseRtspMessage(req.get(), (char *) packet->data, packet->dataLength);
            for (auto option = req->options; option != nullptr; option = option->next) {
              if ("Content-length"sv == option->option) {
                _queue_packet = std::make_pair(peer, std::move(packet));
                return;
              }
            }
          }
          else {
            std::vector<char> full_payload;

            auto old_msg = std::move(_queue_packet);
            TUPLE_2D_REF(_, old_packet, old_msg);

            std::string_view new_payload{(char *) packet->data, packet->dataLength};
            std::string_view old_payload{(char *) old_packet->data, old_packet->dataLength};
            full_payload.resize(new_payload.size() + old_payload.size());

            std::copy(std::begin(old_payload), std::end(old_payload), std::begin(full_payload));
            std::copy(std::begin(new_payload), std::end(new_payload), std::begin(full_payload) + old_payload.size());

            parseRtspMessage(req.get(), full_payload.data(), full_payload.size());
          }

          print_msg(req.get());

          msg_t resp;
          auto func = _map_cmd_cb.find(req->message.request.command);
          if (func != std::end(_map_cmd_cb)) {
            func->second(this, nullptr, peer, std::move(req));
          }
          else {
            cmd_not_found(host(), peer, std::move(req));
          }

          return;
        }
        case ENET_EVENT_TYPE_CONNECT:
          BOOST_LOG(info) << "CLIENT CONNECTED TO RTSP"sv;
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          BOOST_LOG(info) << "CLIENT DISCONNECTED FROM RTSP"sv;
          break;
        case ENET_EVENT_TYPE_NONE:
          break;
      }
    }
  }

  void map(const std::string_view &type, cmd_func_t cb) {
    _map_cmd_cb.emplace(type, std::move(cb));
  }

  void stop() {
    for(auto &slot : _session_slots) {
      auto session = slot.lock();

      if (!session) {
        continue;
      }

      // Wait until the session is properly running
      while (session->state == state_e::STARTING) {
        std::this_thread::sleep_for(1ms);
      }

      ::stream::stop(*session);

      BOOST_LOG(debug) << "Waiting for Audio to end..."sv;
      session->audioThread.join();
      BOOST_LOG(debug) << "Waiting for Video to end..."sv;
      session->videoThread.join();

      input::reset(input);
    }
  }

  bool accept(const std::shared_ptr<session_t> &session) {
    for(auto &slot : _session_slots) {
      if(slot.expired()) {
        slot = session;

        return true;
      }
    }

    return false;
  }

  net::host_t::pointer host() const {
    return _host.get();
  }
private:

  // named _queue_packet because I want to make it an actual queue
  // It's like this for convenience sake
  std::pair<net::peer_t, net::packet_t> _queue_packet;

  std::unordered_map<std::string_view, cmd_func_t> _map_cmd_cb;

  std::vector<std::weak_ptr<session_t>> _session_slots;

  ENetAddress _addr;
  net::host_t _host;
};

void respond(net::host_t::pointer host, net::peer_t peer, msg_t &resp) {
  auto payload = std::make_pair(resp->payload, resp->payloadLength);

  auto lg = util::fail_guard([&]() {
    resp->payload = payload.first;
    resp->payloadLength = payload.second;
  });

  resp->payload = nullptr;
  resp->payloadLength = 0;

  int serialized_len;
  util::c_ptr<char> raw_resp { serializeRtspMessage(resp.get(), &serialized_len) };
  BOOST_LOG(debug)
    << "---Begin Response---"sv << std::endl
    << std::string_view { raw_resp.get(), (std::size_t)serialized_len } << std::endl
    << std::string_view { payload.first, (std::size_t)payload.second } << std::endl
    << "---End Response---"sv << std::endl;

  std::string_view tmp_resp { raw_resp.get(), (size_t)serialized_len };
  {
    auto packet = enet_packet_create(tmp_resp.data(), tmp_resp.size(), ENET_PACKET_FLAG_RELIABLE);
    if(enet_peer_send(peer, 0, packet)) {
      enet_packet_destroy(packet);
      return;
    }

    enet_host_flush(host);
  }

  if(payload.second > 0) {
    auto packet = enet_packet_create(payload.first, payload.second, ENET_PACKET_FLAG_RELIABLE);
    if(enet_peer_send(peer, 0, packet)) {
      enet_packet_destroy(packet);
      return;
    }

    enet_host_flush(host);
  }
}

void respond(net::host_t::pointer host, net::peer_t peer, POPTION_ITEM options, int statuscode, const char *status_msg, int seqn, const std::string_view &payload) {
  msg_t resp { new msg_t::element_type };
  createRtspResponse(resp.get(), nullptr, 0, const_cast<char*>("RTSP/1.0"), statuscode, const_cast<char*>(status_msg), seqn, options, const_cast<char*>(payload.data()), (int)payload.size());

  respond(host, peer, resp);
}

void cmd_not_found(net::host_t::pointer host, net::peer_t peer, msg_t&& req) {
  respond(host, peer, nullptr, 404, "NOT FOUND", req->sequenceNumber, {});
}

void cmd_option(rtsp_server_t *server, const std::shared_ptr<session_t> &session, net::peer_t peer, msg_t&& req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  respond(server->host(), peer, &option, 200, "OK", req->sequenceNumber, {});
}

void cmd_describe(rtsp_server_t *server, const std::shared_ptr<session_t> &session, net::peer_t peer, msg_t&& req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  std::string_view payload;
  if(config::video.hevc_mode == 0) {
    payload = "surround-params=NONE"sv;
  }
  else {
    payload = "sprop-parameter-sets=AAAAAU;surround-params=NONE"sv;
  }

  respond(server->host(), peer, &option, 200, "OK", req->sequenceNumber, payload);
}

void cmd_setup(rtsp_server_t *server, const std::shared_ptr<session_t> &session, net::peer_t peer, msg_t &&req) {
  OPTION_ITEM options[2] {};

  auto &seqn           = options[0];
  auto &session_option = options[1];

  seqn.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  seqn.content = const_cast<char*>(seqn_str.c_str());

  if(session->idr_events) {
    // already streaming

    respond(server->host(), peer, &seqn, 503, "Service Unavailable", req->sequenceNumber, {});
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
    cmd_not_found(server->host(), peer, std::move(req));

    return;
  }

  respond(server->host(), peer, &seqn, 200, "OK", req->sequenceNumber, {});
}

void cmd_announce(rtsp_server_t *server, const std::shared_ptr<session_t> &session, net::peer_t peer, msg_t &&req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  auto expected_state = state_e::STOPPED;
  auto abort = session->state.compare_exchange_strong(expected_state, state_e::STARTING);

  if(abort || !launch_event.peek()) {
    //Either already streaming or /launch has not been used

    if(!abort) {
      session->state.store(state_e::STOPPED);
    }

    respond(server->host(), peer, &option, 503, "Service Unavailable", req->sequenceNumber, {});
    return;
  }
  auto launch_session { launch_event.pop() };

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

        while(pos != std::end(payload) && whitespace(*pos)) { ++pos; }
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

  // Initialize any omitted parameters to defaults
  args.try_emplace("x-nv-video[0].encoderCscMode"sv, "0"sv);
  args.try_emplace("x-nv-vqos[0].bitStreamFormat"sv, "0"sv);
  args.try_emplace("x-nv-video[0].dynamicRangeMode"sv, "0"sv);
  args.try_emplace("x-nv-aqos.packetDuration"sv, "5"sv);

  try {

    auto &config = session->config;
    config.audio.channels       = util::from_view(args.at("x-nv-audio.surround.numChannels"sv));
    config.audio.mask           = util::from_view(args.at("x-nv-audio.surround.channelMask"sv));
    config.audio.packetDuration = util::from_view(args.at("x-nv-aqos.packetDuration"sv));

    config.packetsize = util::from_view(args.at("x-nv-video[0].packetSize"sv));

    config.monitor.height         = util::from_view(args.at("x-nv-video[0].clientViewportHt"sv));
    config.monitor.width          = util::from_view(args.at("x-nv-video[0].clientViewportWd"sv));
    config.monitor.framerate      = util::from_view(args.at("x-nv-video[0].maxFPS"sv));
    config.monitor.bitrate        = util::from_view(args.at("x-nv-vqos[0].bw.maximumBitrateKbps"sv));
    config.monitor.slicesPerFrame = util::from_view(args.at("x-nv-video[0].videoEncoderSlicesPerFrame"sv));
    config.monitor.numRefFrames   = util::from_view(args.at("x-nv-video[0].maxNumReferenceFrames"sv));
    config.monitor.encoderCscMode = util::from_view(args.at("x-nv-video[0].encoderCscMode"sv));
    config.monitor.videoFormat    = util::from_view(args.at("x-nv-vqos[0].bitStreamFormat"sv));
    config.monitor.dynamicRange   = util::from_view(args.at("x-nv-video[0].dynamicRangeMode"sv));

  } catch(std::out_of_range &) {

    respond(server->host(), peer, &option, 400, "BAD REQUEST", req->sequenceNumber, {});
    return;
  }

  if(session->config.monitor.videoFormat != 0 && config::video.hevc_mode == 0) {
    BOOST_LOG(warning) << "HEVC is disabled, yet the client requested HEVC"sv;

    respond(server->host(), peer, &option, 400, "BAD REQUEST", req->sequenceNumber, {});
    return;
  }

  auto &gcm_key  = launch_session->gcm_key;
  auto &iv       = launch_session->iv;

  std::copy(std::begin(gcm_key), std::end(gcm_key), std::begin(session->gcm_key));
  std::copy(std::begin(iv), std::end(iv), std::begin(session->iv));

  session->pingTimeout = std::chrono::steady_clock::now() + config::stream.ping_timeout;

  session->idr_events    = std::make_shared<video::idr_event_t::element_type>();

  session->audioThread   = std::thread {audioThread, session, platf::from_sockaddr((sockaddr*)&peer->address.address)};
  session->videoThread   = std::thread {videoThread, session, platf::from_sockaddr((sockaddr*)&peer->address.address)};

  session->state.store(state_e::RUNNING);
  respond(server->host(), peer, &option, 200, "OK", req->sequenceNumber, {});
}

void cmd_play(rtsp_server_t *server, const std::shared_ptr<session_t> &session, net::peer_t peer, msg_t &&req) {
  OPTION_ITEM option {};

  // I know these string literals will not be modified
  option.option = const_cast<char*>("CSeq");

  auto seqn_str = std::to_string(req->sequenceNumber);
  option.content = const_cast<char*>(seqn_str.c_str());

  respond(server->host(), peer, &option, 200, "OK", req->sequenceNumber, {});
}

void rtpThread(std::shared_ptr<safe::event_t<bool>> shutdown_event) {
  input = std::make_shared<input::input_t>();
  auto fg = util::fail_guard([&]() {
    input.reset();
  });

  rtsp_server_t server(RTSP_SETUP_PORT);

  server.map("OPTIONS"sv, &cmd_option);
  server.map("DESCRIBE"sv, &cmd_describe);
  server.map("SETUP"sv, &cmd_setup);
  server.map("ANNOUNCE"sv, &cmd_announce);

  server.map("PLAY"sv, &cmd_play);

  while(!shutdown_event->peek()) {
    server.iterate(std::min(500ms, config::stream.ping_timeout));
  }

  server.stop();
}

void print_msg(PRTSP_MESSAGE msg) {
  std::string_view type = msg->type == TYPE_RESPONSE ? "RESPONSE"sv : "REQUEST"sv;

  std::string_view payload { msg->payload, (size_t)msg->payloadLength };
  std::string_view protocol { msg->protocol };
  auto seqnm = msg->sequenceNumber;
  std::string_view messageBuffer { msg->messageBuffer };

  BOOST_LOG(debug) << "type ["sv << type << ']';
  BOOST_LOG(debug) << "sequence number ["sv << seqnm << ']';
  BOOST_LOG(debug) << "protocol :: "sv << protocol;
  BOOST_LOG(debug) << "payload :: "sv << payload;

  if(msg->type == TYPE_RESPONSE) {
    auto &resp = msg->message.response;

    auto statuscode = resp.statusCode;
    std::string_view status { resp.statusString };

    BOOST_LOG(debug) << "statuscode :: "sv << statuscode;
    BOOST_LOG(debug) << "status :: "sv << status;
  }
  else {
    auto& req = msg->message.request;

    std::string_view command { req.command };
    std::string_view target { req.target };

    BOOST_LOG(debug) << "command :: "sv << command;
    BOOST_LOG(debug) << "target :: "sv << target;
  }

  for(auto option = msg->options; option != nullptr; option = option->next) {
    std::string_view content { option->content };
    std::string_view name { option->option };

    BOOST_LOG(debug) << name << " :: "sv << content;
  }

  BOOST_LOG(debug) << "---Begin MessageBuffer---"sv << std::endl << messageBuffer << std::endl << "---End MessageBuffer---"sv << std::endl;
}
}