/**
 * @file tests/unit/platform/test_common.cpp
 * @brief Test src/platform/common.*.
 */

// test includes
#include "../../tests_common.h"

// standard includes
#include <array>
#include <chrono>
#include <string_view>
#include <tuple>
#include <vector>

// lib includes
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/udp.hpp>

// local includes
#include <src/platform/common.h>

TEST(HostnameTests, TestAsioEquality) {
  // These should be equivalent on all platforms for ASCII hostnames
  ASSERT_EQ(platf::get_host_name(), boost::asio::ip::host_name());
}

TEST(SocketAddressTest, FormatsConstIpv4AndIpv6AddressesAndPreservesNetworkOrderPorts) {
  for (const auto *text : {"192.0.2.17", "2001:db8::17"}) {
    const boost::asio::ip::udp::endpoint endpoint {boost::asio::ip::make_address(text), 4321};
    EXPECT_EQ(platf::from_sockaddr(endpoint.data()), text);
    const auto [port, address] = platf::from_sockaddr_ex(endpoint.data());
    EXPECT_EQ(port, htons(4321));
    EXPECT_EQ(address, text);
  }
}

TEST(SocketAddressTest, UnsupportedAddressFamiliesProduceEmptyAddressesAndZeroPorts) {
  const sockaddr address {};
  EXPECT_TRUE(platf::from_sockaddr(&address).empty());
  EXPECT_EQ(platf::from_sockaddr_ex(&address), (std::pair<std::uint16_t, std::string> {0, {}}));
}

/**
 * @brief Const batch descriptors locate payload boundaries and reject offsets past all buffers.
 */
TEST(BatchedSendInfoTest, ConstLookupPreservesBuffersAndFindsEveryBoundary) {
  const std::string_view first = "abc";
  const std::string_view second = "defg";
  std::vector<platf::buffer_descriptor_t> buffers {{nullptr, 0}, {first.data(), first.size()}, {second.data(), second.size()}};
  auto address = boost::asio::ip::address_v4::loopback();
  boost::asio::ip::address endpoint = address;
  const platf::batched_send_info_t batch {nullptr, 0, buffers, 1, 0, 7, 0, endpoint, 0, endpoint};

  EXPECT_EQ(batch.buffer_for_payload_offset(0).buffer, first.data());
  EXPECT_EQ(batch.buffer_for_payload_offset(0).size, first.size());
  EXPECT_EQ(batch.buffer_for_payload_offset(2).buffer, first.data() + 2);
  EXPECT_EQ(batch.buffer_for_payload_offset(2).size, 1);
  EXPECT_EQ(batch.buffer_for_payload_offset(3).buffer, second.data());
  EXPECT_EQ(batch.buffer_for_payload_offset(3).size, second.size());
  EXPECT_EQ(batch.buffer_for_payload_offset(6).buffer, second.data() + 3);
  EXPECT_EQ(batch.buffer_for_payload_offset(6).size, 1);
  for (const auto offset : {7, 8}) {
    EXPECT_EQ(batch.buffer_for_payload_offset(offset).buffer, nullptr);
    EXPECT_EQ(batch.buffer_for_payload_offset(offset).size, 0);
  }
  EXPECT_EQ(buffers.size(), 3);
  EXPECT_EQ(buffers[1].buffer, first.data());
  EXPECT_EQ(buffers[1].size, first.size());
}

#if defined(_WIN32) || defined(__linux__) || defined(__FreeBSD__)
/**
 * @brief Exercise source-address control messages on bound UDP sockets with optional headers.
 */
class UdpSourceAddressTest: public testing::TestWithParam<std::tuple<bool, bool, bool>> {};

TEST_P(UdpSourceAddressTest, SendsTheExpectedDatagramFromTheRequestedAddress) {
  const auto [ipv6, batched, with_header] = GetParam();
  auto address = boost::asio::ip::make_address(ipv6 ? "::1" : "127.0.0.1");
  boost::asio::io_context context;
  boost::asio::ip::udp::socket receiver(context, {address, 0});
  // FreeBSD requires a bound port before accepting IP_SENDSRCADDR.
  boost::asio::ip::udp::socket sender(context, {receiver.local_endpoint().protocol(), 0});
  const auto port = receiver.local_endpoint().port();
  const auto native_socket = static_cast<std::uintptr_t>(sender.native_handle());
  const std::string_view header = "header";
  const std::string_view payload = "payload";
  if (batched) {
    const std::string_view headers = "headerheader";
    const std::string_view payloads = "payloadpayload";
    std::vector<platf::buffer_descriptor_t> buffers {{payloads.data(), payloads.size()}};
    const platf::batched_send_info_t batch_info {
      with_header ? headers.data() : nullptr,
      with_header ? header.size() : 0,
      buffers,
      payload.size(),
      0,
      2,
      native_socket,
      address,
      port,
      address
    };
    ASSERT_TRUE(platf::send_batch(batch_info));
  } else {
    platf::send_info_t packet_info {
      with_header ? header.data() : nullptr,
      with_header ? header.size() : 0,
      payload.data(),
      payload.size(),
      native_socket,
      address,
      port,
      address
    };
    ASSERT_TRUE(platf::send(packet_info));
  }

  for (auto packet = 0; packet < (batched ? 2 : 1); ++packet) {
    std::array<char, 64> bytes;
    boost::asio::ip::udp::endpoint peer;
    boost::system::error_code receive_error;
    std::size_t received = 0;
    bool completed = false;
    receiver.async_receive_from(boost::asio::buffer(bytes), peer, [&receive_error, &received, &completed](const auto &result, std::size_t size) {
      receive_error = result;
      received = size;
      completed = true;
    });
    context.restart();
    context.run_for(std::chrono::seconds {2});
    ASSERT_TRUE(completed);
    ASSERT_FALSE(receive_error) << receive_error.message();
    EXPECT_EQ(peer.address(), address);
    EXPECT_EQ(std::string_view(bytes.data(), received), with_header ? "headerpayload" : "payload");
  }
}

INSTANTIATE_TEST_SUITE_P(
  Loopback,
  UdpSourceAddressTest,
  testing::Combine(testing::Bool(), testing::Bool(), testing::Bool())
);
#endif
