/**
 * @file src/rstp.cpp
 * @brief todo
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

extern "C" {
#include <moonlight-common-c/src/Rtsp.h>
}

#include <array>
#include <cctype>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "config.h"
#include "input.h"
#include "main.h"
#include "network.h"
#include "rtsp.h"
#include "stream.h"
#include "sync.h"
#include "video.h"

#include <unordered_map>

namespace asio = boost::asio;

using asio::ip::tcp;
using asio::ip::udp;

using namespace std::literals;

namespace rtsp_stream {
  void
  free_msg(PRTSP_MESSAGE msg) {
    freeMessage(msg);

    delete msg;
  }

  class rtsp_server_t;

  using msg_t = util::safe_ptr<RTSP_MESSAGE, free_msg>;
  using cmd_func_t = std::function<void(rtsp_server_t *server, tcp::socket &, msg_t &&)>;

  void
  print_msg(PRTSP_MESSAGE msg);
  void
  cmd_not_found(tcp::socket &sock, msg_t &&req);
  void
  respond(tcp::socket &sock, POPTION_ITEM options, int statuscode, const char *status_msg, int seqn, const std::string_view &payload);

  class socket_t: public std::enable_shared_from_this<socket_t> {
  public:
    socket_t(boost::asio::io_service &ios, std::function<void(tcp::socket &sock, msg_t &&)> &&handle_data_fn):
        handle_data_fn { std::move(handle_data_fn) }, sock { ios } {}

    void
    read() {
      if (begin == std::end(msg_buf)) {
        BOOST_LOG(error) << "RTSP: read(): Exceeded maximum rtsp packet size: "sv << msg_buf.size();

        respond(sock, nullptr, 400, "BAD REQUEST", 0, {});

        sock.close();

        return;
      }

      sock.async_read_some(
        boost::asio::buffer(begin, (std::size_t)(std::end(msg_buf) - begin)),
        boost::bind(
          &socket_t::handle_read, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
    }

    void
    read_payload() {
      if (begin == std::end(msg_buf)) {
        BOOST_LOG(error) << "RTSP: read_payload(): Exceeded maximum rtsp packet size: "sv << msg_buf.size();

        respond(sock, nullptr, 400, "BAD REQUEST", 0, {});

        sock.close();

        return;
      }

      sock.async_read_some(
        boost::asio::buffer(begin, (std::size_t)(std::end(msg_buf) - begin)),
        boost::bind(
          &socket_t::handle_payload, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
    }

    static void
    handle_payload(std::shared_ptr<socket_t> &socket, const boost::system::error_code &ec, std::size_t bytes) {
      BOOST_LOG(debug) << "handle_payload(): Handle read of size: "sv << bytes << " bytes"sv;

      auto sock_close = util::fail_guard([&socket]() {
        boost::system::error_code ec;
        socket->sock.close(ec);

        if (ec) {
          BOOST_LOG(error) << "RTSP: handle_payload(): Couldn't close tcp socket: "sv << ec.message();
        }
      });

      if (ec) {
        BOOST_LOG(error) << "RTSP: handle_payload(): Couldn't read from tcp socket: "sv << ec.message();

        return;
      }

      auto end = socket->begin + bytes;
      msg_t req { new msg_t::element_type {} };
      if (auto status = parseRtspMessage(req.get(), socket->msg_buf.data(), (std::size_t)(end - socket->msg_buf.data()))) {
        BOOST_LOG(error) << "Malformed RTSP message: ["sv << status << ']';

        respond(socket->sock, nullptr, 400, "BAD REQUEST", req->sequenceNumber, {});
        return;
      }

      sock_close.disable();

      auto fg = util::fail_guard([&socket]() {
        socket->read_payload();
      });

      auto content_length = 0;
      for (auto option = req->options; option != nullptr; option = option->next) {
        if ("Content-length"sv == option->option) {
          BOOST_LOG(debug) << "Found Content-Length: "sv << option->content << " bytes"sv;

          // If content_length > bytes read, then we need to store current data read,
          // to be appended by the next read.
          std::string_view content { option->content };
          auto begin = std::find_if(std::begin(content), std::end(content), [](auto ch) { return (bool) std::isdigit(ch); });

          content_length = util::from_chars(begin, std::end(content));
          break;
        }
      }

      if (end - socket->crlf >= content_length) {
        if (end - socket->crlf > content_length) {
          BOOST_LOG(warning) << "(end - socket->crlf) > content_length -- "sv << (std::size_t)(end - socket->crlf) << " > "sv << content_length;
        }

        fg.disable();
        print_msg(req.get());

        socket->handle_data(std::move(req));
      }

      socket->begin = end;
    }

    static void
    handle_read(std::shared_ptr<socket_t> &socket, const boost::system::error_code &ec, std::size_t bytes) {
      BOOST_LOG(debug) << "handle_read(): Handle read of size: "sv << bytes << " bytes"sv;

      if (ec) {
        BOOST_LOG(error) << "RTSP: handle_read(): Couldn't read from tcp socket: "sv << ec.message();

        boost::system::error_code ec;
        socket->sock.close(ec);

        if (ec) {
          BOOST_LOG(error) << "RTSP: handle_read(): Couldn't close tcp socket: "sv << ec.message();
        }

        return;
      }

      auto fg = util::fail_guard([&socket]() {
        socket->read();
      });

      auto begin = std::max(socket->begin - 4, socket->begin);
      auto buf_size = bytes + (begin - socket->begin);
      auto end = begin + buf_size;

      constexpr auto needle = "\r\n\r\n"sv;

      auto it = std::search(begin, begin + buf_size, std::begin(needle), std::end(needle));
      if (it == end) {
        socket->begin = end;

        return;
      }

      // Emulate read completion for payload data
      socket->begin = it + needle.size();
      socket->crlf = socket->begin;
      buf_size = end - socket->begin;

      fg.disable();
      handle_payload(socket, ec, buf_size);
    }

    void
    handle_data(msg_t &&req) {
      handle_data_fn(sock, std::move(req));
    }

    std::function<void(tcp::socket &sock, msg_t &&)> handle_data_fn;

    tcp::socket sock;

    std::array<char, 2048> msg_buf;

    char *crlf;
    char *begin = msg_buf.data();
  };

  class rtsp_server_t {
  public:
    ~rtsp_server_t() {
      clear();
    }

    int
    bind(net::af_e af, std::uint16_t port, boost::system::error_code &ec) {
      {
        auto lg = _session_slots.lock();

        _session_slots->resize(config::stream.channels);
        _slot_count = config::stream.channels;
      }

      acceptor.open(af == net::IPV4 ? tcp::v4() : tcp::v6(), ec);
      if (ec) {
        return -1;
      }

      acceptor.set_option(boost::asio::socket_base::reuse_address { true });

      acceptor.bind(tcp::endpoint(af == net::IPV4 ? tcp::v4() : tcp::v6(), port), ec);
      if (ec) {
        return -1;
      }

      acceptor.listen(4096, ec);
      if (ec) {
        return -1;
      }

      next_socket = std::make_shared<socket_t>(ios, [this](tcp::socket &sock, msg_t &&msg) {
        handle_msg(sock, std::move(msg));
      });

      acceptor.async_accept(next_socket->sock, [this](const auto &ec) {
        handle_accept(ec);
      });

      return 0;
    }

    template <class T, class X>
    void
    iterate(std::chrono::duration<T, X> timeout) {
      ios.run_one_for(timeout);
    }

    void
    handle_msg(tcp::socket &sock, msg_t &&req) {
      auto func = _map_cmd_cb.find(req->message.request.command);
      if (func != std::end(_map_cmd_cb)) {
        func->second(this, sock, std::move(req));
      }
      else {
        cmd_not_found(sock, std::move(req));
      }

      sock.shutdown(boost::asio::socket_base::shutdown_type::shutdown_both);
    }

    void
    handle_accept(const boost::system::error_code &ec) {
      if (ec) {
        BOOST_LOG(error) << "Couldn't accept incoming connections: "sv << ec.message();

        // Stop server
        clear();
        return;
      }

      auto socket = std::move(next_socket);
      socket->read();

      next_socket = std::make_shared<socket_t>(ios, [this](tcp::socket &sock, msg_t &&msg) {
        handle_msg(sock, std::move(msg));
      });

      acceptor.async_accept(next_socket->sock, [this](const auto &ec) {
        handle_accept(ec);
      });
    }

    void
    map(const std::string_view &type, cmd_func_t cb) {
      _map_cmd_cb.emplace(type, std::move(cb));
    }

    /**
     * @brief Launch a new streaming session.
     * @note If the client does not begin streaming within the ping_timeout,
     *       the session will be discarded.
     * @param launch_session Streaming session information.
     *
     * EXAMPLES:
     * ```cpp
     * launch_session_t launch_session;
     * rtsp_server_t server {};
     * server.session_raise(launch_session);
     * ```
     */
    void
    session_raise(rtsp_stream::launch_session_t launch_session) {
      auto now = std::chrono::steady_clock::now();

      // If a launch event is still pending, don't overwrite it.
      if (raised_timeout > now && launch_event.peek()) {
        return;
      }
      raised_timeout = now + config::stream.ping_timeout;

      --_slot_count;
      launch_event.raise(launch_session);
    }

    int
    session_count() const {
      return config::stream.channels - _slot_count;
    }

    safe::event_t<rtsp_stream::launch_session_t> launch_event;

    /**
     * @brief Clear launch sessions.
     * @param all If true, clear all sessions. Otherwise, only clear timed out and stopped sessions.
     *
     * EXAMPLES:
     * ```cpp
     * clear(false);
     * ```
     */
    void
    clear(bool all = true) {
      // if a launch event timed out --> Remove it.
      if (raised_timeout < std::chrono::steady_clock::now()) {
        auto discarded = launch_event.pop(0s);
        if (discarded) {
          BOOST_LOG(debug) << "Event timeout: "sv << discarded->unique_id;
          ++_slot_count;
        }
      }

      auto lg = _session_slots.lock();

      for (auto &slot : *_session_slots) {
        if (slot && (all || stream::session::state(*slot) == stream::session::state_e::STOPPING)) {
          stream::session::stop(*slot);
          stream::session::join(*slot);

          slot.reset();

          ++_slot_count;
        }
      }

      if (all && !ios.stopped()) {
        ios.stop();
      }
    }

    void
    clear(std::shared_ptr<stream::session_t> *session_p) {
      auto lg = _session_slots.lock();

      session_p->reset();

      ++_slot_count;
    }

    std::shared_ptr<stream::session_t> *
    accept(std::shared_ptr<stream::session_t> &session) {
      auto lg = _session_slots.lock();

      for (auto &slot : *_session_slots) {
        if (!slot) {
          slot = session;
          return &slot;
        }
      }

      return nullptr;
    }

  private:
    std::unordered_map<std::string_view, cmd_func_t> _map_cmd_cb;

    sync_util::sync_t<std::vector<std::shared_ptr<stream::session_t>>> _session_slots;

    std::chrono::steady_clock::time_point raised_timeout;
    int _slot_count;

    boost::asio::io_service ios;
    tcp::acceptor acceptor { ios };

    std::shared_ptr<socket_t> next_socket;
  };

  rtsp_server_t server {};

  void
  launch_session_raise(rtsp_stream::launch_session_t launch_session) {
    server.session_raise(launch_session);
  }

  int
  session_count() {
    // Ensure session_count is up-to-date
    server.clear(false);

    return server.session_count();
  }

  int
  send(tcp::socket &sock, const std::string_view &sv) {
    std::size_t bytes_send = 0;

    while (bytes_send != sv.size()) {
      boost::system::error_code ec;
      bytes_send += sock.send(boost::asio::buffer(sv.substr(bytes_send)), 0, ec);

      if (ec) {
        BOOST_LOG(error) << "RTSP: Couldn't send data over tcp socket: "sv << ec.message();
        return -1;
      }
    }

    return 0;
  }

  void
  respond(tcp::socket &sock, msg_t &resp) {
    auto payload = std::make_pair(resp->payload, resp->payloadLength);

    // Restore response message for proper destruction
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
      << std::string_view { raw_resp.get(), (std::size_t) serialized_len } << std::endl
      << std::string_view { payload.first, (std::size_t) payload.second } << std::endl
      << "---End Response---"sv << std::endl;

    std::string_view tmp_resp { raw_resp.get(), (size_t) serialized_len };

    if (send(sock, tmp_resp)) {
      return;
    }

    send(sock, std::string_view { payload.first, (std::size_t) payload.second });
  }

  void
  respond(tcp::socket &sock, POPTION_ITEM options, int statuscode, const char *status_msg, int seqn, const std::string_view &payload) {
    msg_t resp { new msg_t::element_type };
    createRtspResponse(resp.get(), nullptr, 0, const_cast<char *>("RTSP/1.0"), statuscode, const_cast<char *>(status_msg), seqn, options, const_cast<char *>(payload.data()), (int) payload.size());

    respond(sock, resp);
  }

  void
  cmd_not_found(tcp::socket &sock, msg_t &&req) {
    respond(sock, nullptr, 404, "NOT FOUND", req->sequenceNumber, {});
  }

  void
  cmd_option(rtsp_server_t *server, tcp::socket &sock, msg_t &&req) {
    OPTION_ITEM option {};

    // I know these string literals will not be modified
    option.option = const_cast<char *>("CSeq");

    auto seqn_str = std::to_string(req->sequenceNumber);
    option.content = const_cast<char *>(seqn_str.c_str());

    respond(sock, &option, 200, "OK", req->sequenceNumber, {});
  }

  void
  cmd_describe(rtsp_server_t *server, tcp::socket &sock, msg_t &&req) {
    OPTION_ITEM option {};

    // I know these string literals will not be modified
    option.option = const_cast<char *>("CSeq");

    auto seqn_str = std::to_string(req->sequenceNumber);
    option.content = const_cast<char *>(seqn_str.c_str());

    std::stringstream ss;

    // Tell the client about our supported features
    ss << "a=x-ss-general.featureFlags: " << (uint32_t) platf::get_capabilities() << std::endl;

    if (video::active_hevc_mode != 1) {
      ss << "sprop-parameter-sets=AAAAAU"sv << std::endl;
    }

    if (video::last_encoder_probe_supported_ref_frames_invalidation) {
      ss << "x-nv-video[0].refPicInvalidation=1"sv << std::endl;
    }

    if (video::active_av1_mode != 1) {
      ss << "a=rtpmap:98 AV1/90000"sv << std::endl;
    }

    for (int x = 0; x < audio::MAX_STREAM_CONFIG; ++x) {
      auto &stream_config = audio::stream_configs[x];
      std::uint8_t mapping[platf::speaker::MAX_SPEAKERS];

      auto mapping_p = stream_config.mapping;

      /**
       * GFE advertises incorrect mapping for normal quality configurations,
       * as a result, Moonlight rotates all channels from index '3' to the right
       * To work around this, rotate channels to the left from index '3'
       */
      if (x == audio::SURROUND51 || x == audio::SURROUND71) {
        std::copy_n(mapping_p, stream_config.channelCount, mapping);
        std::rotate(mapping + 3, mapping + 4, mapping + audio::MAX_STREAM_CONFIG);

        mapping_p = mapping;
      }

      ss << "a=fmtp:97 surround-params="sv << stream_config.channelCount << stream_config.streams << stream_config.coupledStreams;

      std::for_each_n(mapping_p, stream_config.channelCount, [&ss](std::uint8_t digit) {
        ss << (char) (digit + '0');
      });

      ss << std::endl;
    }

    respond(sock, &option, 200, "OK", req->sequenceNumber, ss.str());
  }

  void
  cmd_setup(rtsp_server_t *server, tcp::socket &sock, msg_t &&req) {
    OPTION_ITEM options[4] {};

    auto &seqn = options[0];
    auto &session_option = options[1];
    auto &port_option = options[2];
    auto &payload_option = options[3];

    seqn.option = const_cast<char *>("CSeq");

    auto seqn_str = std::to_string(req->sequenceNumber);
    seqn.content = const_cast<char *>(seqn_str.c_str());

    if (!server->launch_event.peek()) {
      // /launch has not been used

      respond(sock, &seqn, 503, "Service Unavailable", req->sequenceNumber, {});
      return;
    }
    auto launch_session { server->launch_event.view() };

    std::string_view target { req->message.request.target };
    auto begin = std::find(std::begin(target), std::end(target), '=') + 1;
    auto end = std::find(begin, std::end(target), '/');
    std::string_view type { begin, (size_t) std::distance(begin, end) };

    std::uint16_t port;
    if (type == "audio"sv) {
      port = map_port(stream::AUDIO_STREAM_PORT);
    }
    else if (type == "video"sv) {
      port = map_port(stream::VIDEO_STREAM_PORT);
    }
    else if (type == "control"sv) {
      port = map_port(stream::CONTROL_PORT);
    }
    else {
      cmd_not_found(sock, std::move(req));

      return;
    }

    seqn.next = &session_option;

    session_option.option = const_cast<char *>("Session");
    session_option.content = const_cast<char *>("DEADBEEFCAFE;timeout = 90");

    session_option.next = &port_option;

    // Moonlight merely requires 'server_port=<port>'
    auto port_value = "server_port=" + std::to_string(port);

    port_option.option = const_cast<char *>("Transport");
    port_option.content = port_value.data();

    // Send identifiers that will be echoed in the other connections
    auto connect_data = std::to_string(launch_session->control_connect_data);
    if (type == "control"sv) {
      payload_option.option = const_cast<char *>("X-SS-Connect-Data");
      payload_option.content = connect_data.data();
    }
    else {
      payload_option.option = const_cast<char *>("X-SS-Ping-Payload");
      payload_option.content = launch_session->av_ping_payload.data();
    }

    port_option.next = &payload_option;

    respond(sock, &seqn, 200, "OK", req->sequenceNumber, {});
  }

  void
  cmd_announce(rtsp_server_t *server, tcp::socket &sock, msg_t &&req) {
    OPTION_ITEM option {};

    // I know these string literals will not be modified
    option.option = const_cast<char *>("CSeq");

    auto seqn_str = std::to_string(req->sequenceNumber);
    option.content = const_cast<char *>(seqn_str.c_str());

    if (!server->launch_event.peek()) {
      // /launch has not been used

      respond(sock, &option, 503, "Service Unavailable", req->sequenceNumber, {});
      return;
    }
    auto launch_session { server->launch_event.pop() };

    std::string_view payload { req->payload, (size_t) req->payloadLength };

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

          while (pos != std::end(payload) && whitespace(*pos)) { ++pos; }
          begin = pos;
        }
      }
    }

    std::string_view client;
    std::unordered_map<std::string_view, std::string_view> args;

    for (auto line : lines) {
      auto type = line.substr(0, 2);
      if (type == "s="sv) {
        client = line.substr(2);
      }
      else if (type == "a=") {
        auto pos = line.find(':');

        auto name = line.substr(2, pos - 2);
        auto val = line.substr(pos + 1);

        if (val[val.size() - 1] == ' ') {
          val = val.substr(0, val.size() - 1);
        }
        args.emplace(name, val);
      }
    }

    // Initialize any omitted parameters to defaults
    args.try_emplace("x-nv-video[0].encoderCscMode"sv, "0"sv);
    args.try_emplace("x-nv-vqos[0].bitStreamFormat"sv, "0"sv);
    args.try_emplace("x-nv-video[0].dynamicRangeMode"sv, "0"sv);
    args.try_emplace("x-nv-aqos.packetDuration"sv, "5"sv);
    args.try_emplace("x-nv-general.useReliableUdp"sv, "1"sv);
    args.try_emplace("x-nv-vqos[0].fec.minRequiredFecPackets"sv, "0"sv);
    args.try_emplace("x-nv-general.featureFlags"sv, "135"sv);
    args.try_emplace("x-ml-general.featureFlags"sv, "0"sv);
    args.try_emplace("x-nv-vqos[0].qosTrafficType"sv, "5"sv);
    args.try_emplace("x-nv-aqos.qosTrafficType"sv, "4"sv);
    args.try_emplace("x-ml-video.configuredBitrateKbps"sv, "0"sv);

    stream::config_t config;

    std::int64_t configuredBitrateKbps;
    config.audio.flags[audio::config_t::HOST_AUDIO] = launch_session->host_audio;
    try {
      config.audio.channels = util::from_view(args.at("x-nv-audio.surround.numChannels"sv));
      config.audio.mask = util::from_view(args.at("x-nv-audio.surround.channelMask"sv));
      config.audio.packetDuration = util::from_view(args.at("x-nv-aqos.packetDuration"sv));

      config.audio.flags[audio::config_t::HIGH_QUALITY] =
        util::from_view(args.at("x-nv-audio.surround.AudioQuality"sv));

      config.controlProtocolType = util::from_view(args.at("x-nv-general.useReliableUdp"sv));
      config.packetsize = util::from_view(args.at("x-nv-video[0].packetSize"sv));
      config.minRequiredFecPackets = util::from_view(args.at("x-nv-vqos[0].fec.minRequiredFecPackets"sv));
      config.nvFeatureFlags = util::from_view(args.at("x-nv-general.featureFlags"sv));
      config.mlFeatureFlags = util::from_view(args.at("x-ml-general.featureFlags"sv));
      config.audioQosType = util::from_view(args.at("x-nv-aqos.qosTrafficType"sv));
      config.videoQosType = util::from_view(args.at("x-nv-vqos[0].qosTrafficType"sv));

      config.monitor.height = util::from_view(args.at("x-nv-video[0].clientViewportHt"sv));
      config.monitor.width = util::from_view(args.at("x-nv-video[0].clientViewportWd"sv));
      config.monitor.framerate = util::from_view(args.at("x-nv-video[0].maxFPS"sv));
      config.monitor.bitrate = util::from_view(args.at("x-nv-vqos[0].bw.maximumBitrateKbps"sv));
      config.monitor.slicesPerFrame = util::from_view(args.at("x-nv-video[0].videoEncoderSlicesPerFrame"sv));
      config.monitor.numRefFrames = util::from_view(args.at("x-nv-video[0].maxNumReferenceFrames"sv));
      config.monitor.encoderCscMode = util::from_view(args.at("x-nv-video[0].encoderCscMode"sv));
      config.monitor.videoFormat = util::from_view(args.at("x-nv-vqos[0].bitStreamFormat"sv));
      config.monitor.dynamicRange = util::from_view(args.at("x-nv-video[0].dynamicRangeMode"sv));

      configuredBitrateKbps = util::from_view(args.at("x-ml-video.configuredBitrateKbps"sv));
    }
    catch (std::out_of_range &) {
      respond(sock, &option, 400, "BAD REQUEST", req->sequenceNumber, {});
      return;
    }

    // When using stereo audio, the audio quality is (strangely) indicated by whether the Host field
    // in the RTSP message matches a local interface's IP address. Fortunately, Moonlight always sends
    // 0.0.0.0 when it wants low quality, so it is easy to check without enumerating interfaces.
    if (config.audio.channels == 2) {
      for (auto option = req->options; option != nullptr; option = option->next) {
        if ("Host"sv == option->option) {
          std::string_view content { option->content };
          BOOST_LOG(debug) << "Found Host: "sv << content;
          config.audio.flags[audio::config_t::HIGH_QUALITY] = (content.find("0.0.0.0"sv) == std::string::npos);
        }
      }
    }

    // If the client sent a configured bitrate, we will choose the actual bitrate ourselves
    // by using FEC percentage and audio quality settings. If the calculated bitrate ends up
    // too low, we'll allow it to exceed the limits rather than reducing the encoding bitrate
    // down to nearly nothing.
    if (configuredBitrateKbps) {
      BOOST_LOG(debug) << "Client configured bitrate is "sv << configuredBitrateKbps << " Kbps"sv;

      // If the FEC percentage isn't too high, adjust the configured bitrate to ensure video
      // traffic doesn't exceed the user's selected bitrate when the FEC shards are included.
      if (config::stream.fec_percentage <= 80) {
        configuredBitrateKbps /= 100.f / (100 - config::stream.fec_percentage);
      }

      // Adjust the bitrate to account for audio traffic bandwidth usage (capped at 20% reduction).
      // The bitrate per channel is 256 Kbps for high quality mode and 96 Kbps for normal quality.
      auto audioBitrateAdjustment = (config.audio.flags[audio::config_t::HIGH_QUALITY] ? 256 : 96) * config.audio.channels;
      configuredBitrateKbps -= std::min((std::int64_t) audioBitrateAdjustment, configuredBitrateKbps / 5);

      // Reduce it by another 500Kbps to account for A/V packet overhead and control data
      // traffic (capped at 10% reduction).
      configuredBitrateKbps -= std::min((std::int64_t) 500, configuredBitrateKbps / 10);

      BOOST_LOG(debug) << "Final adjusted video encoding bitrate is "sv << configuredBitrateKbps << " Kbps"sv;
      config.monitor.bitrate = configuredBitrateKbps;
    }

    if (config.monitor.videoFormat == 1 && video::active_hevc_mode == 1) {
      BOOST_LOG(warning) << "HEVC is disabled, yet the client requested HEVC"sv;

      respond(sock, &option, 400, "BAD REQUEST", req->sequenceNumber, {});
      return;
    }

    if (config.monitor.videoFormat == 2 && video::active_av1_mode == 1) {
      BOOST_LOG(warning) << "AV1 is disabled, yet the client requested AV1"sv;

      respond(sock, &option, 400, "BAD REQUEST", req->sequenceNumber, {});
      return;
    }

    auto session = stream::session::alloc(config, launch_session->gcm_key, launch_session->iv, launch_session->av_ping_payload, launch_session->control_connect_data);

    auto slot = server->accept(session);
    if (!slot) {
      BOOST_LOG(info) << "Ran out of slots for client from ["sv << ']';

      respond(sock, &option, 503, "Service Unavailable", req->sequenceNumber, {});
      return;
    }

    if (stream::session::start(*session, sock.remote_endpoint().address().to_string())) {
      BOOST_LOG(error) << "Failed to start a streaming session"sv;

      server->clear(slot);
      respond(sock, &option, 500, "Internal Server Error", req->sequenceNumber, {});
      return;
    }

    respond(sock, &option, 200, "OK", req->sequenceNumber, {});
  }

  void
  cmd_play(rtsp_server_t *server, tcp::socket &sock, msg_t &&req) {
    OPTION_ITEM option {};

    // I know these string literals will not be modified
    option.option = const_cast<char *>("CSeq");

    auto seqn_str = std::to_string(req->sequenceNumber);
    option.content = const_cast<char *>(seqn_str.c_str());

    respond(sock, &option, 200, "OK", req->sequenceNumber, {});
  }

  void
  rtpThread() {
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);
    auto broadcast_shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);

    server.map("OPTIONS"sv, &cmd_option);
    server.map("DESCRIBE"sv, &cmd_describe);
    server.map("SETUP"sv, &cmd_setup);
    server.map("ANNOUNCE"sv, &cmd_announce);

    server.map("PLAY"sv, &cmd_play);

    boost::system::error_code ec;
    if (server.bind(net::af_from_enum_string(config::sunshine.address_family), map_port(rtsp_stream::RTSP_SETUP_PORT), ec)) {
      BOOST_LOG(fatal) << "Couldn't bind RTSP server to port ["sv << map_port(rtsp_stream::RTSP_SETUP_PORT) << "], " << ec.message();
      shutdown_event->raise(true);

      return;
    }

    while (!shutdown_event->peek()) {
      server.iterate(std::min(500ms, config::stream.ping_timeout));

      if (broadcast_shutdown_event->peek()) {
        server.clear();
      }
      else {
        // cleanup all stopped sessions
        server.clear(false);
      }
    }

    server.clear();
  }

  void
  print_msg(PRTSP_MESSAGE msg) {
    std::string_view type = msg->type == TYPE_RESPONSE ? "RESPONSE"sv : "REQUEST"sv;

    std::string_view payload { msg->payload, (size_t) msg->payloadLength };
    std::string_view protocol { msg->protocol };
    auto seqnm = msg->sequenceNumber;
    std::string_view messageBuffer { msg->messageBuffer };

    BOOST_LOG(debug) << "type ["sv << type << ']';
    BOOST_LOG(debug) << "sequence number ["sv << seqnm << ']';
    BOOST_LOG(debug) << "protocol :: "sv << protocol;
    BOOST_LOG(debug) << "payload :: "sv << payload;

    if (msg->type == TYPE_RESPONSE) {
      auto &resp = msg->message.response;

      auto statuscode = resp.statusCode;
      std::string_view status { resp.statusString };

      BOOST_LOG(debug) << "statuscode :: "sv << statuscode;
      BOOST_LOG(debug) << "status :: "sv << status;
    }
    else {
      auto &req = msg->message.request;

      std::string_view command { req.command };
      std::string_view target { req.target };

      BOOST_LOG(debug) << "command :: "sv << command;
      BOOST_LOG(debug) << "target :: "sv << target;
    }

    for (auto option = msg->options; option != nullptr; option = option->next) {
      std::string_view content { option->content };
      std::string_view name { option->option };

      BOOST_LOG(debug) << name << " :: "sv << content;
    }

    BOOST_LOG(debug) << "---Begin MessageBuffer---"sv << std::endl
                     << messageBuffer << std::endl
                     << "---End MessageBuffer---"sv << std::endl;
  }
}  // namespace rtsp_stream
