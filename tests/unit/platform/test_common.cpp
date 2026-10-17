/**
 * @file tests/unit/platform/test_common.cpp
 * @brief Test src/platform/common.*.
 */

// test includes
#include "../../tests_common.h"

// standard includes
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
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
/**
 * @brief The host and CUDA image layouts share the same field block, and
 *        derived images expose it through the base class.
 */
TEST(PixelFormatTests, ImgLayoutContract) {
  static_assert(std::is_base_of_v<platf::img_layout_t, platf::img_t>);

  platf::img_t img {};
  platf::img_layout_t &layout = img;
  layout.pixel_format = platf::pix_fmt_e::bgr0;
  layout.row_pitch = 400;
  EXPECT_EQ(img.pixel_format, platf::pix_fmt_e::bgr0);
  EXPECT_EQ(img.row_pitch, 400);
}

/**
 * @brief Capture formats representable as packed 8-bit BGR are accepted by the RAM
 *        conversion paths, while other declared formats are rejected.
 */
TEST(PixelFormatTests, IsBgrCaptureFormat) {
  EXPECT_TRUE(platf::is_bgr_capture_format(platf::pix_fmt_e::unknown));
  EXPECT_TRUE(platf::is_bgr_capture_format(platf::pix_fmt_e::bgr0));
  EXPECT_TRUE(platf::is_bgr_capture_format(platf::pix_fmt_e::bgra));

  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::nv12));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::p010));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv420p));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv420p10));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::ayuv));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv444p));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv444p16));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::y410));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::rgba));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::xbgr2101010));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::bgra1010102));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::rgba1010102));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::abgr2101010));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::argb2101010));
}

/**
 * @brief Undeclared capture images are only accepted as BGR when they report
 *        (or derive) 4 bytes per pixel; declared packed BGR formats are 4
 *        bytes per pixel by definition and other formats are rejected.
 */
TEST(PixelFormatTests, IsBgrCaptureImage) {
  platf::img_t img {};

  // Undeclared layouts are trusted only at the historical 4 bytes per pixel.
  img.pixel_format = platf::pix_fmt_e::unknown;
  img.pixel_pitch = 4;
  EXPECT_TRUE(platf::is_bgr_capture_image(img));

  img.pixel_pitch = 3;
  EXPECT_FALSE(platf::is_bgr_capture_image(img));
  img.pixel_pitch = 1;
  EXPECT_FALSE(platf::is_bgr_capture_image(img));

  // Backends that only report the row pitch have the pixel pitch derived from
  // it.
  img.pixel_pitch = 0;
  img.width = 100;
  img.row_pitch = 400;
  EXPECT_TRUE(platf::is_bgr_capture_image(img));

  img.row_pitch = 300;
  EXPECT_FALSE(platf::is_bgr_capture_image(img));

  // Declared packed BGR formats define 4 bytes per pixel.
  img.pixel_format = platf::pix_fmt_e::bgr0;
  img.pixel_pitch = 4;
  img.row_pitch = 400;
  EXPECT_TRUE(platf::is_bgr_capture_image(img));

  // The declaration wins over a disagreeing pixel pitch; the row layout is
  // validated separately by the upload paths.
  img.pixel_pitch = 3;
  img.row_pitch = 300;
  EXPECT_TRUE(platf::is_bgr_capture_image(img));
  EXPECT_FALSE(platf::packed_4byte_row_layout_valid(img));

  // Negative pitches are invalid and must not be masked by a derived value.
  img.pixel_format = platf::pix_fmt_e::unknown;
  img.pixel_pitch = -4;
  img.row_pitch = 400;
  EXPECT_EQ(img.bytes_per_pixel(), 0);
  EXPECT_FALSE(platf::is_bgr_capture_image(img));

  img.pixel_pitch = 0;
  img.row_pitch = -400;
  EXPECT_FALSE(platf::is_bgr_capture_image(img));

  img.pixel_format = platf::pix_fmt_e::bgra;
  img.pixel_pitch = 0;
  img.row_pitch = 448;
  EXPECT_TRUE(platf::is_bgr_capture_image(img));

  // Formats that are not packed 8-bit BGR are rejected even at 4 bytes per
  // pixel.
  img.pixel_format = platf::pix_fmt_e::rgba;
  img.pixel_pitch = 4;
  EXPECT_FALSE(platf::is_bgr_capture_image(img));

  img.pixel_format = platf::pix_fmt_e::nv12;
  img.pixel_pitch = 1;
  EXPECT_FALSE(platf::is_bgr_capture_image(img));

  img.pixel_format = platf::pix_fmt_e::p010;
  img.pixel_pitch = 2;
  EXPECT_FALSE(platf::is_bgr_capture_image(img));
}

/**
 * @brief A row pitch must describe complete 4-byte rows before an upload.
 */
TEST(PixelFormatTests, Packed4ByteRowLayoutValid) {
  platf::img_t img {};
  img.width = 100;

  img.row_pitch = 400;
  EXPECT_TRUE(platf::packed_4byte_row_layout_valid(img));

  // Padded rows are fine as long as they are a whole number of pixels.
  img.row_pitch = 448;
  EXPECT_TRUE(platf::packed_4byte_row_layout_valid(img));

  // Shorter than one row.
  img.row_pitch = 300;
  EXPECT_FALSE(platf::packed_4byte_row_layout_valid(img));

  // Not a whole number of pixels.
  img.row_pitch = 401;
  EXPECT_FALSE(platf::packed_4byte_row_layout_valid(img));

  // Negative pitches cannot describe a row.
  img.row_pitch = -400;
  EXPECT_FALSE(platf::packed_4byte_row_layout_valid(img));

  // Degenerate images cannot describe a row.
  img.width = 0;
  img.row_pitch = 400;
  EXPECT_FALSE(platf::packed_4byte_row_layout_valid(img));

  // A width whose row cannot fit in a 32-bit pitch is rejected through the
  // 64-bit comparison instead of overflowing.
  EXPECT_FALSE(platf::packed_4byte_row_layout_valid(std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max()));
}

/**
 * @brief The bytes per pixel are reported directly, or derived from the row
 *        pitch for backends that do not report them. Degenerate images report
 *        no bytes per pixel.
 */
TEST(PixelFormatTests, BytesPerPixel) {
  platf::img_t img {};

  img.pixel_pitch = 4;
  EXPECT_EQ(img.bytes_per_pixel(), 4);

  img.pixel_pitch = 0;
  img.width = 100;
  img.row_pitch = 448;
  EXPECT_EQ(img.bytes_per_pixel(), 4);

  img.row_pitch = 300;
  EXPECT_EQ(img.bytes_per_pixel(), 3);

  // Negative pitches are invalid, not a request to derive the value.
  img.pixel_pitch = -4;
  img.row_pitch = 400;
  EXPECT_EQ(img.bytes_per_pixel(), 0);

  img.pixel_pitch = 0;
  img.row_pitch = -400;
  EXPECT_EQ(img.bytes_per_pixel(), 0);

  img.row_pitch = 400;
  img.width = 0;
  EXPECT_EQ(img.bytes_per_pixel(), 0);
}

TEST(PixelFormatTests, FromPixFmt) {
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv420p), "yuv420p");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv420p10), "yuv420p10");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::nv12), "nv12");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::p010), "p010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::ayuv), "ayuv");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv444p16), "yuv444p16");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv444p), "yuv444p");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::y410), "y410");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::bgr0), "bgr0");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::bgra), "bgra");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::rgba), "rgba");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::xbgr2101010), "xbgr2101010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::bgra1010102), "bgra1010102");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::rgba1010102), "rgba1010102");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::abgr2101010), "abgr2101010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::argb2101010), "argb2101010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::unknown), "unknown");

  // Values outside the declared enumerators fall back to "unknown".
  EXPECT_EQ(platf::from_pix_fmt(static_cast<platf::pix_fmt_e>(0x7fffffff)), "unknown");
}

/**
 * @brief The diagnostic string reports the declared format and the resolved
 *        bytes per pixel used in the upload error paths.
 */
TEST(PixelFormatTests, DescribeCaptureImage) {
  platf::img_t img {};
  img.pixel_format = platf::pix_fmt_e::bgr0;
  img.pixel_pitch = 4;
  EXPECT_EQ(platf::describe_capture_image(img), "(format: bgr0, bytes per pixel: 4)");

  // Undeclared layouts derive the size from the row pitch.
  img.pixel_format = platf::pix_fmt_e::unknown;
  img.pixel_pitch = 0;
  img.width = 100;
  img.row_pitch = 300;
  EXPECT_EQ(platf::describe_capture_image(img), "(format: unknown, bytes per pixel: 3)");
}
