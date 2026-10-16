/**
 * @file tests/unit/test_rtsp.cpp
 * @brief Verify RTSP response handlers through their common dispatcher signature.
 */

// local includes
// Include before test helpers so transitive atomic includes cannot hide C++ templates inside C linkage.
#include <src/compat/moonlight.h>

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <string>
#include <utility>
#include <vector>

// lib includes
#include <boost/asio.hpp>
extern "C" {
#include <moonlight-common-c/src/Rtsp.h>
}

// local includes
#include <src/audio.h>
#include <src/rtsp.h>
#include <src/video.h>

/**
 * @brief The Moonlight compatibility header can be included first before using the C RTSP parser.
 */
TEST(RtspHeaderTest, MoonlightCompatibilityHeaderCanPrecedeRtspParser) {
  char request[] = "OPTIONS rtsp://localhost RTSP/1.0\r\nCSeq: 42\r\n\r\n";
  RTSP_MESSAGE message {};
  ASSERT_EQ(parseRtspMessage(&message, request, sizeof(request) - 1), RTSP_ERROR_SUCCESS);
  const auto cleanup = util::fail_guard([&message]() {
    freeMessage(&message);
  });

  EXPECT_EQ(message.type, TYPE_REQUEST);
  EXPECT_EQ(message.sequenceNumber, 42);
  EXPECT_STREQ(message.message.request.command, "OPTIONS");
}

/**
 * @brief Const RTSP wire headers decode network-order lengths independently of encryption flags.
 */
TEST(RtspHeaderTest, ConstAccessorsPreserveTheSerializedHeader) {
  using rtsp_stream::encrypted_rtsp_header_t;
  for (const auto length : {0U, 1U, 0x7fffffffU}) {
    for (const bool encrypted : {false, true}) {
      const auto flags = encrypted ? encrypted_rtsp_header_t::ENCRYPTED_MESSAGE_TYPE_BIT : 0U;
      const encrypted_rtsp_header_t header {util::endian::big(length | flags), 0, {}};
      EXPECT_EQ(header.payload_length(), length);
      EXPECT_EQ(header.is_encrypted(), encrypted);
      EXPECT_EQ(header.typeAndLength, util::endian::big(length | flags));
    }
  }
}

namespace rtsp_stream {
  class rtsp_server_t;

  /**
   * @brief Release a parsed RTSP message and its allocation.
   * @param message Message to release.
   */
  void free_msg(PRTSP_MESSAGE message);

  /**
   * @brief Owning RTSP message type used by the dispatcher.
   */
  using msg_t = util::safe_ptr<RTSP_MESSAGE, &free_msg>;

  /**
   * @brief Respond to OPTIONS through the common dispatcher signature.
   * @param sock Connected client socket.
   * @param session Launch session.
   * @param req Request to handle.
   */
  void cmd_option(rtsp_server_t *, boost::asio::ip::tcp::socket &sock, launch_session_t &session, msg_t &&req);

  /**
   * @brief Respond to DESCRIBE through the common dispatcher signature.
   * @param sock Connected client socket.
   * @param session Launch session.
   * @param req Request to handle.
   */
  void cmd_describe(rtsp_server_t *, boost::asio::ip::tcp::socket &sock, launch_session_t &session, msg_t &&req);

  /**
   * @brief Respond to PLAY through the common dispatcher signature.
   * @param sock Connected client socket.
   * @param session Launch session.
   * @param req Request to handle.
   */
  void cmd_play(rtsp_server_t *, boost::asio::ip::tcp::socket &sock, launch_session_t &session, msg_t &&req);

  /**
   * @brief Validate ANNOUNCE before starting a stream.
   * @param sock Connected client socket.
   * @param session Launch session.
   * @param req Request to handle.
   */
  void cmd_announce(rtsp_server_t *, boost::asio::ip::tcp::socket &sock, launch_session_t &session, msg_t &&req);
}  // namespace rtsp_stream

namespace {
  /**
   * @brief Exercise codec negotiation on a local RTSP connection without starting a stream.
   * @param describe Whether to request DESCRIBE instead of an invalid ANNOUNCE.
   * @param payload SDP body for ANNOUNCE.
   * @return Complete RTSP response.
   */
  std::string pyrowave_rtsp_response(bool describe, std::string payload = {}) {
    using boost::asio::ip::tcp;
    boost::asio::io_context io;
    tcp::acceptor acceptor {io, {boost::asio::ip::address_v4::loopback(), 0}};
    tcp::socket client {io};
    client.connect(acceptor.local_endpoint());
    auto server_socket = acceptor.accept();
    rtsp_stream::launch_session_t session {};
    rtsp_stream::msg_t request {new RTSP_MESSAGE {}};
    request->sequenceNumber = 17;
    request->payload = payload.data();
    request->payloadLength = int(payload.size());
    if (describe) {
      rtsp_stream::cmd_describe(nullptr, server_socket, session, std::move(request));
    } else {
      rtsp_stream::cmd_announce(nullptr, server_socket, session, std::move(request));
    }
    server_socket.shutdown(tcp::socket::shutdown_send);
    std::string response;
    boost::system::error_code error;
    boost::asio::read(client, boost::asio::dynamic_buffer(response), error);
    EXPECT_EQ(error, boost::asio::error::eof);
    return response;
  }
}  // namespace

TEST(RtspPyrowaveTest, AdvertisesOnlyProbedProfiles) {
  const auto previous = video::active_pyrowave_mode;
  const auto restore = util::fail_guard([previous]() {
    video::active_pyrowave_mode = previous;
  });
  for (int mode : {0, 2, 3}) {
    video::active_pyrowave_mode = mode;
    auto response = pyrowave_rtsp_response(true);
    EXPECT_EQ(response.find("a=rtpmap:99 PYROWAVE/90000") != std::string::npos, mode >= 2);
    EXPECT_EQ(response.find("a=x-ss-video[0].pyrowaveFrameFormat:PYRW/1") != std::string::npos, mode >= 2);
    EXPECT_NE(response.find("CSeq: 17"), std::string::npos);
  }
}

TEST(RtspPyrowaveTest, RejectsUnavailableHdrAndUnknownFormatsBeforeStartingCapture) {
  const auto previous = video::active_pyrowave_mode;
  const auto restore = util::fail_guard([previous]() {
    video::active_pyrowave_mode = previous;
  });
  const std::string base =
    "s=Moonlight\r\n"
    "a=x-nv-audio.surround.numChannels:2\r\n"
    "a=x-nv-audio.surround.channelMask:3\r\n"
    "a=x-nv-audio.surround.AudioQuality:1\r\n"
    "a=x-nv-video[0].packetSize:1392\r\n"
    "a=x-nv-video[0].clientViewportHt:1080\r\n"
    "a=x-nv-video[0].clientViewportWd:1920\r\n"
    "a=x-nv-video[0].maxFPS:60\r\n"
    "a=x-nv-vqos[0].bw.maximumBitrateKbps:200000\r\n"
    "a=x-nv-video[0].videoEncoderSlicesPerFrame:1\r\n"
    "a=x-nv-video[0].maxNumReferenceFrames:0\r\n";
  for (const auto &[mode, format, hdr, chroma] : std::array {
         std::array {0, 3, 0, 0},
         std::array {2, 3, 1, 0},
         std::array {3, 3, 2, 0},
         std::array {3, 3, 0, 1},
         std::array {3, -1, 0, 0},
         std::array {3, 4, 0, 0}
       }) {
    video::active_pyrowave_mode = mode;
    auto payload = base + "a=x-nv-vqos[0].bitStreamFormat:" + std::to_string(format) + "\r\n" +
                   "a=x-nv-video[0].dynamicRangeMode:" + std::to_string(hdr) + "\r\n" +
                   "a=x-ss-video[0].chromaSamplingType:" + std::to_string(chroma) + "\r\n";
    EXPECT_TRUE(pyrowave_rtsp_response(false, payload).starts_with("RTSP/1.0 400 BAD REQUEST\r\n"));
  }
}

/**
 * @brief All three handlers respond with the request's sequence even without a server instance.
 */
TEST(RtspResponseTest, CommonDispatcherPreservesResponsesWithoutServerContext) {
  using boost::asio::ip::tcp;
  const std::array handlers {
    &rtsp_stream::cmd_option,
    &rtsp_stream::cmd_describe,
    &rtsp_stream::cmd_play,
  };

  for (const auto handler : handlers) {
    boost::asio::io_context io;
    tcp::acceptor acceptor {io, {boost::asio::ip::address_v4::loopback(), 0}};
    tcp::socket client {io};
    client.connect(acceptor.local_endpoint());
    auto server_socket = acceptor.accept();

    rtsp_stream::launch_session_t session {};
    rtsp_stream::msg_t request {new RTSP_MESSAGE {}};
    request->sequenceNumber = 42;
    handler(nullptr, server_socket, session, std::move(request));
    server_socket.shutdown(tcp::socket::shutdown_send);

    std::string response;
    boost::system::error_code read_error;
    boost::asio::read(client, boost::asio::dynamic_buffer(response), read_error);
    ASSERT_EQ(read_error, boost::asio::error::eof);
    EXPECT_TRUE(response.starts_with("RTSP/1.0 200 OK\r\n"));
    EXPECT_NE(response.find("CSeq: 42\r\n"), std::string::npos);

    if (handler == &rtsp_stream::cmd_describe) {
      EXPECT_NE(response.find("a=x-ss-general.featureFlags:"), std::string::npos);
      EXPECT_NE(response.find("a=x-ss-general.encryptionSupported:"), std::string::npos);
      EXPECT_NE(response.find("a=fmtp:97 surround-params="), std::string::npos);
    } else {
      EXPECT_TRUE(response.ends_with("\r\n\r\n"));
    }
  }
}

/**
 * @brief Surround mappings compensate for the client swap over the channels of each layout, so every
 * speaker lands on its own position once the client applies its rotation.
 */
TEST(RtspDescribeTest, SurroundParamsCompensateClientRotationForEveryStreamConfig) {
  using boost::asio::ip::tcp;

  boost::asio::io_context io;
  tcp::acceptor acceptor {io, {boost::asio::ip::address_v4::loopback(), 0}};
  tcp::socket client {io};
  client.connect(acceptor.local_endpoint());
  auto server_socket = acceptor.accept();

  rtsp_stream::launch_session_t session {};
  rtsp_stream::msg_t request {new RTSP_MESSAGE {}};
  rtsp_stream::cmd_describe(nullptr, server_socket, session, std::move(request));
  server_socket.shutdown(tcp::socket::shutdown_send);

  std::string response;
  boost::system::error_code read_error;
  boost::asio::read(client, boost::asio::dynamic_buffer(response), read_error);
  ASSERT_EQ(read_error, boost::asio::error::eof);

  const std::string params_prefix = "a=fmtp:97 surround-params=";
  std::vector<std::string> advertised;
  for (auto start = response.find(params_prefix); start != std::string::npos;
       start = response.find(params_prefix, start + params_prefix.size())) {
    const auto end = response.find('\n', start);
    advertised.push_back(response.substr(start + params_prefix.size(), end - start - params_prefix.size()));
  }

  // moonlight-common-c moves the last speaker of a surround layout to index 3 and shifts the rest up,
  // so the compensation must cover the channels of the layout being advertised rather than a fixed range.
  const std::vector<std::pair<int, std::string>> expected {
    {audio::STEREO, "21101"},
    {audio::HIGH_STEREO, "21101"},
    {audio::SURROUND51, "642012453"},
    {audio::HIGH_SURROUND51, "660012345"},
    {audio::SURROUND71, "85301245673"},
    {audio::HIGH_SURROUND71, "88001234567"},
  };

  ASSERT_EQ(advertised.size(), expected.size());
  for (std::size_t x = 0; x < expected.size(); ++x) {
    EXPECT_EQ(advertised[x], expected[x].second) << "stream config " << expected[x].first;
  }
}
