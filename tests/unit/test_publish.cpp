/**
 * @file tests/unit/test_publish.cpp
 * @brief Tests for Linux mDNS (Avahi) service publication.
 */

#if defined(__linux__)

  // test includes
  #include "../tests_common.h"

  // standard includes
  #include <array>
  #include <utility>
  #include <vector>

  // local includes
  #include <src/platform/linux/publish.h>

TEST(ServiceHostNameTest, IsDistinctFromDaemonHostName) {
  // The SRV target must not be the daemon's shared host name: the daemon
  // publishes both A and AAAA records for its own host name, so pointing the
  // service at it would advertise an IPv6 endpoint an IPv4-only
  // configuration does not serve (see #5860).
  ASSERT_EQ(platf::publish::service_host_name("myhost", 0), "myhost-sunshine.local");
}

TEST(ServiceHostNameTest, SanitizesInvalidLabelCharacters) {
  ASSERT_EQ(platf::publish::service_host_name("my host.example", 0), "my-host-sunshine.local");
  ASSERT_EQ(platf::publish::service_host_name("", 0), "sunshine-sunshine.local");
}

TEST(ServiceHostNameTest, CollisionSuffixChangesName) {
  ASSERT_EQ(platf::publish::service_host_name("myhost", 1), "myhost-sunshine-1.local");
  ASSERT_NE(platf::publish::service_host_name("myhost", 0), platf::publish::service_host_name("myhost", 1));
}

TEST(ServiceHostNameTest, LabelStaysWithinDnsLimit) {
  const auto host_name = platf::publish::service_host_name(std::string(100, 'a'), 0);
  // The label (everything before ".local") must fit in 63 characters.
  ASSERT_TRUE(host_name.find('.') <= 63);
}

TEST(SelectServedAddressesTest, Ipv4OnlyPublishesNoIpv6) {
  // Regression core for #5860: with an IPv4-only configuration the selection
  // contains no IPv6 address, so no AAAA endpoint is published for the
  // service host name.
  const auto selected = platf::publish::select_served_addresses(
    {boost::asio::ip::make_address("192.168.1.10"),
     boost::asio::ip::make_address("fe80::1"),
     boost::asio::ip::make_address("2001:db8::1")},
    net::IPV4
  );
  ASSERT_EQ(selected.size(), 1U);
  ASSERT_EQ(selected[0], boost::asio::ip::make_address("192.168.1.10"));
}

TEST(SelectServedAddressesTest, BothPublishesBothFamilies) {
  const auto selected = platf::publish::select_served_addresses(
    {boost::asio::ip::make_address("192.168.1.10"), boost::asio::ip::make_address("2001:db8::1")},
    net::BOTH
  );
  ASSERT_EQ(selected.size(), 2U);
}

TEST(SelectServedAddressesTest, DropsUnusableAndDuplicateAddresses) {
  const auto selected = platf::publish::select_served_addresses(
    {boost::asio::ip::make_address("127.0.0.1"),
     boost::asio::ip::make_address("0.0.0.0"),
     boost::asio::ip::make_address("::1"),
     boost::asio::ip::make_address("192.168.1.10"),
     boost::asio::ip::make_address("192.168.1.10"),
     boost::asio::ip::make_address("::ffff:192.168.1.10")},
    net::BOTH
  );
  ASSERT_EQ(selected.size(), 1U);
  ASSERT_EQ(selected[0], boost::asio::ip::make_address("192.168.1.10"));
}

TEST(ToAvahiAddressTest, Ipv4EncodesNetworkOrderBytes) {
  const auto address = platf::publish::to_avahi_address(boost::asio::ip::make_address("192.168.1.10"));
  ASSERT_EQ(address.proto, std::to_underlying(avahi::proto::PROTO_INET));
  ASSERT_EQ(address.data, (std::array<std::uint8_t, 16> {192, 168, 1, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(ToAvahiAddressTest, Ipv6EncodesAllSixteenBytes) {
  const auto address = platf::publish::to_avahi_address(boost::asio::ip::make_address("2001:db8::1"));
  ASSERT_EQ(address.proto, std::to_underlying(avahi::proto::PROTO_INET6));
  ASSERT_EQ(
    address.data,
    (std::array<std::uint8_t, 16> {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01})
  );
}

#endif  // defined(__linux__)
