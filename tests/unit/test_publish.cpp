/**
 * @file tests/unit/test_publish.cpp
 * @brief Tests for Linux mDNS (Avahi) service publication.
 */

#if defined(__linux__)

// test includes
#include "../tests_common.h"

// local includes
#include <src/platform/linux/publish.h>

TEST(PublishProtocolTest, Ipv4MapsToProtoInet) {
  // With address_family = ipv4, Avahi must only publish IPv4 records;
  // previously the service was always registered with PROTO_UNSPEC, which
  // also advertised an IPv6 address nothing was listening on (#5860).
  ASSERT_EQ(platf::publish::protocol_for_address_family(net::IPV4), static_cast<avahi::Protocol>(avahi::proto::PROTO_INET));
}

TEST(PublishProtocolTest, BothMapsToProtoUnspec) {
  // With address_family = both, keep the previous behavior: advertise for
  // all protocols.
  ASSERT_EQ(platf::publish::protocol_for_address_family(net::BOTH), static_cast<avahi::Protocol>(avahi::proto::PROTO_UNSPEC));
}

#endif  // defined(__linux__)
