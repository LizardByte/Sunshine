/**
 * @file tests/unit/test_rtsp.cpp
 * @brief Verify RTSP response handlers through their common dispatcher signature.
 */

// standard includes
#include <array>
#include <string>
#include <utility>

// lib includes
#include <boost/asio.hpp>
extern "C" {
#include <moonlight-common-c/src/Rtsp.h>
}

// test includes
#include "../tests_common.h"

// local includes
#include <src/config.h>
#include <src/rtsp.h>
#include <src/video.h>

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
  const auto previous = video::active_pyrowave_profiles;
  const auto restore = util::fail_guard([previous]() {
    video::active_pyrowave_profiles = previous;
  });
  for (uint32_t profiles = 0; profiles < 16; ++profiles) {
    video::active_pyrowave_profiles = profiles << 23;
    auto response = pyrowave_rtsp_response(true);
    EXPECT_EQ(response.find("a=rtpmap:99 PYROWAVE/90000") != std::string::npos, profiles != 0);
    EXPECT_EQ(response.find("a=x-ss-video[0].pyrowaveFrameFormat:PYRW/1") != std::string::npos, profiles != 0);
    EXPECT_NE(response.find("CSeq: 17"), std::string::npos);
  }
}

/**
 * @brief Validate every profile independently, stopping accepted requests at the encryption gate.
 */
TEST(RtspPyrowaveTest, NegotiatesProbedProfilesAndRejectsUnknownFormatsBeforeStartingCapture) {
  const auto previous = video::active_pyrowave_profiles;
  const auto previous_lan = config::stream.lan_encryption_mode;
  const auto previous_wan = config::stream.wan_encryption_mode;
  const auto restore = util::fail_guard([=]() {
    video::active_pyrowave_profiles = previous;
    config::stream.lan_encryption_mode = previous_lan;
    config::stream.wan_encryption_mode = previous_wan;
  });
  config::stream.lan_encryption_mode = config::ENCRYPTION_MODE_MANDATORY;
  config::stream.wan_encryption_mode = config::ENCRYPTION_MODE_MANDATORY;
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
  for (uint32_t profiles = 0; profiles < 16; ++profiles) {
    video::active_pyrowave_profiles = profiles << 23;
    for (const auto &[format, hdr, chroma] : std::array {
           std::array {3, 0, 0},
           std::array {3, 0, 1},
           std::array {3, 1, 0},
           std::array {3, 1, 1},
           std::array {3, 2, 0},
           std::array {3, 0, 2},
           std::array {-1, 0, 0},
           std::array {4, 0, 0}
         }) {
      auto payload = base + "a=x-nv-vqos[0].bitStreamFormat:" + std::to_string(format) + "\r\n" +
                     "a=x-nv-video[0].dynamicRangeMode:" + std::to_string(hdr) + "\r\n" +
                     "a=x-ss-video[0].chromaSamplingType:" + std::to_string(chroma) + "\r\n";
      const bool supported = format == 3 && hdr < 2 && chroma < 2 && (profiles & (1 << (hdr * 2 + chroma)));
      EXPECT_TRUE(pyrowave_rtsp_response(false, payload).starts_with(supported ? "RTSP/1.0 403 Forbidden\r\n" : "RTSP/1.0 400 BAD REQUEST\r\n"))
        << "profiles " << profiles << ", format " << format << ", HDR " << hdr << ", chroma " << chroma;
    }
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
