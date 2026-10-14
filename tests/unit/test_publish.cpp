/**
 * @file tests/unit/test_publish.cpp
 * @brief Tests for Linux mDNS (Avahi) service publication.
 */

#if defined(__linux__)

  // test includes
  #include "../tests_common.h"

  // standard includes
  #include <array>
  #include <cstdlib>
  #include <cstring>
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
    {{1, boost::asio::ip::make_address("192.168.1.10")},
     {1, boost::asio::ip::make_address("fe80::1")},
     {2, boost::asio::ip::make_address("2001:db8::1")}},
    net::IPV4
  );
  ASSERT_EQ(selected.size(), 1U);
  ASSERT_EQ(selected[0].interface, 1);
  ASSERT_EQ(selected[0].address, boost::asio::ip::make_address("192.168.1.10"));
}

TEST(SelectServedAddressesTest, BothPublishesBothFamilies) {
  const auto selected = platf::publish::select_served_addresses(
    {{1, boost::asio::ip::make_address("192.168.1.10")}, {2, boost::asio::ip::make_address("2001:db8::1")}},
    net::BOTH
  );
  ASSERT_EQ(selected.size(), 2U);
  ASSERT_EQ(selected[0].interface, 1);
  ASSERT_EQ(selected[1].interface, 2);
}

TEST(SelectServedAddressesTest, DropsUnusableAndDuplicateAddresses) {
  const auto selected = platf::publish::select_served_addresses(
    {{1, boost::asio::ip::make_address("127.0.0.1")},
     {1, boost::asio::ip::make_address("0.0.0.0")},
     {1, boost::asio::ip::make_address("::1")},
     {1, boost::asio::ip::make_address("192.168.1.10")},
     {1, boost::asio::ip::make_address("192.168.1.10")},
     {1, boost::asio::ip::make_address("::ffff:192.168.1.10")}},
    net::BOTH
  );
  ASSERT_EQ(selected.size(), 1U);
  ASSERT_EQ(selected[0].address, boost::asio::ip::make_address("192.168.1.10"));
}

TEST(SelectServedAddressesTest, DeduplicatesByInterfaceAndAddress) {
  // The same address on two interfaces is reachable on both, so it is kept
  // once per interface; the same interface-plus-address pair is kept once.
  // A v4-mapped duplicate normalizes to the same IPv4 address.
  const auto selected = platf::publish::select_served_addresses(
    {{1, boost::asio::ip::make_address("192.168.1.10")},
     {1, boost::asio::ip::make_address("192.168.1.10")},
     {2, boost::asio::ip::make_address("192.168.1.10")},
     {1, boost::asio::ip::make_address("::ffff:192.168.1.10")}},
    net::BOTH
  );
  ASSERT_EQ(selected.size(), 2U);
  ASSERT_EQ(selected[0], (platf::publish::ServedAddress {1, boost::asio::ip::make_address("192.168.1.10")}));
  ASSERT_EQ(selected[1], (platf::publish::ServedAddress {2, boost::asio::ip::make_address("192.168.1.10")}));
}

TEST(ServedAddressesTest, ExplicitBindResolvesOwningInterface) {
  // An explicit bind address is published with the interface index of the
  // interface that owns it, not IF_UNSPEC.
  const auto enumerated = platf::publish::enumerate_interface_addresses();
  if (enumerated.empty()) {
    GTEST_SKIP() << "No local interface addresses to resolve a bind address against.";
  }

  const auto &entry = enumerated[0];
  const auto saved_bind = config::sunshine.bind_address;
  config::sunshine.bind_address = entry.address.to_string();

  const auto served = platf::publish::served_addresses(net::BOTH);

  config::sunshine.bind_address = saved_bind;

  ASSERT_EQ(served.size(), 1U);
  ASSERT_EQ(served[0], entry);
}

TEST(ServedAddressesTest, ExplicitBindWithoutOwnerFallsBackToUnspec) {
  // A bind address no local interface owns is still published exactly as
  // configured, with IF_UNSPEC.
  const auto saved_bind = config::sunshine.bind_address;
  // TEST-NET-1 (RFC 5737) is never assigned to a real interface, so no
  // local interface can own it.
  config::sunshine.bind_address = "192.0.2.10";

  const auto served = platf::publish::served_addresses(net::BOTH);

  config::sunshine.bind_address = saved_bind;

  ASSERT_EQ(served.size(), 1U);
  ASSERT_EQ(served[0].interface, avahi::IF_UNSPEC);
  ASSERT_EQ(served[0].address, boost::asio::ip::make_address("192.0.2.10"));
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

namespace {
  /**
   * @brief Captured Avahi entry-group calls for registration tests.
   */
  struct AddressCall {
    avahi::IfIndex interface;  ///< Interface index the record was published on.
    avahi::PublishFlags flags;  ///< Flags the record was published with.
    avahi::Address address;  ///< Address record published.
  };

  std::vector<AddressCall> g_address_calls;  ///< Address-publication calls captured by the stub.
  std::vector<std::string> g_service_hosts;  ///< SRV-target host names captured by the stub.
  int g_reset_count = 0;  ///< Entry-group resets captured by the stub.
  int g_commit_count = 0;  ///< Entry-group commits captured by the stub.
  int g_quit_count = 0;  ///< Poll-loop quits captured by the stub.
  bool g_add_service_collide_once = false;  ///< Next add-service call reports a collision.

  /**
   * @brief Avahi error code for a name collision (see `avahi::err_e`).
   */
  constexpr int kErrCollision = -8;

  void FakeFree(void *p) {
    std::free(p);
  }

  void FakeClientFree(avahi::Client * /*client*/) {
    // The test client is a fake pointer; nothing to release.
  }

  char *FakeAlternativeServiceName(char *name) {
    return ::strdup(name);
  }

  char *FakeStrerror(int /*error*/) {
    return const_cast<char *>("stub Avahi error");
  }

  int FakeClientErrno(avahi::Client * /*client*/) {
    return 0;
  }

  void FakeSimplePollQuit(avahi::SimplePoll * /*poll*/) {
    ++g_quit_count;
  }

  int FakeEntryGroupIsEmpty(avahi::EntryGroup * /*group*/) {
    return 1;
  }

  int FakeEntryGroupReset(avahi::EntryGroup * /*group*/) {
    ++g_reset_count;
    return 0;
  }

  int FakeEntryGroupCommit(avahi::EntryGroup * /*group*/) {
    ++g_commit_count;
    return 0;
  }

  int FakeEntryGroupAddService(
    avahi::EntryGroup * /*group*/,
    avahi::IfIndex /*interface*/,
    avahi::Protocol /*protocol*/,
    avahi::PublishFlags /*flags*/,
    const char * /*name*/,
    const char * /*type*/,
    const char * /*domain*/,
    const char *host,
    std::uint16_t /*port*/,
    ...
  ) {
    g_service_hosts.emplace_back(host ? host : "");
    if (g_add_service_collide_once) {
      g_add_service_collide_once = false;
      return kErrCollision;
    }
    return 0;
  }

  /**
   * @brief Stub address publication that models Avahi's reverse-PTR conflict.
   * @details With the daemon's default `publish-addresses=yes`, the daemon
   * already owns the reverse PTR for each local IP. Publishing the same IP
   * for the service host without `PUBLISH_NO_REVERSE` creates a competing
   * unique reverse PTR and Avahi rejects it with `ERR_COLLISION`.
   */
  int FakeEntryGroupAddAddress(
    avahi::EntryGroup * /*group*/,
    avahi::IfIndex interface,
    avahi::Protocol /*protocol*/,
    avahi::PublishFlags flags,
    const char * /*name*/,
    const avahi::Address *address
  ) {
    if ((flags & avahi::PUBLISH_NO_REVERSE) == 0) {
      return kErrCollision;
    }
    g_address_calls.push_back({interface, flags, *address});
    return 0;
  }

  /**
   * @brief Stub address publication without the reverse-PTR conflict check.
   * @details The F2/F3 tests install this stub so their fail-before
   * evidence isolates their own finding: with the strict stub, the missing
   * `PUBLISH_NO_REVERSE` flag (F1) would fail those tests first.
   */
  int FakeEntryGroupAddAddressLenient(
    avahi::EntryGroup * /*group*/,
    avahi::IfIndex interface,
    avahi::Protocol /*protocol*/,
    avahi::PublishFlags flags,
    const char * /*name*/,
    const avahi::Address *address
  ) {
    g_address_calls.push_back({interface, flags, *address});
    return 0;
  }

  /**
   * @brief Fixture driving the publisher with stubbed Avahi calls.
   */
  class PublishRegistrationTest: public ::testing::Test {
  protected:
    void SetUp() override {
      saved_bind_address_ = config::sunshine.bind_address;
      saved_address_family_ = config::sunshine.address_family;

      avahi::free = FakeFree;
      avahi::client_free = FakeClientFree;
      avahi::alternative_service_name = FakeAlternativeServiceName;
      avahi::strerror = FakeStrerror;
      avahi::client_errno = FakeClientErrno;
      avahi::simple_poll_quit = FakeSimplePollQuit;
      avahi::entry_group_is_empty = FakeEntryGroupIsEmpty;
      avahi::entry_group_reset = FakeEntryGroupReset;
      avahi::entry_group_commit = FakeEntryGroupCommit;
      avahi::entry_group_add_service = FakeEntryGroupAddService;
      avahi::entry_group_add_address = FakeEntryGroupAddAddress;

      g_address_calls.clear();
      g_service_hosts.clear();
      g_reset_count = 0;
      g_commit_count = 0;
      g_quit_count = 0;
      g_add_service_collide_once = false;

      platf::publish::group = reinterpret_cast<avahi::EntryGroup *>(0x1000);
      platf::publish::client.reset(reinterpret_cast<avahi::Client *>(0x2000));
      platf::publish::name.reset(::strdup("test-host"));
      platf::publish::host_name_collision_count = 0;
      platf::publish::published_addresses.clear();
    }

    void TearDown() override {
      // Release owned state through the stubs before uninstalling them:
      // the deleters call the stubbed Avahi free functions.
      platf::publish::client.reset();
      platf::publish::name.reset();
      platf::publish::group = nullptr;
      platf::publish::host_name_collision_count = 0;
      platf::publish::published_addresses.clear();

      avahi::free = nullptr;
      avahi::client_free = nullptr;
      avahi::alternative_service_name = nullptr;
      avahi::strerror = nullptr;
      avahi::client_errno = nullptr;
      avahi::simple_poll_quit = nullptr;
      avahi::entry_group_is_empty = nullptr;
      avahi::entry_group_reset = nullptr;
      avahi::entry_group_commit = nullptr;
      avahi::entry_group_add_service = nullptr;
      avahi::entry_group_add_address = nullptr;

      config::sunshine.bind_address = saved_bind_address_;
      config::sunshine.address_family = saved_address_family_;
    }

    std::string saved_bind_address_;
    std::string saved_address_family_;
  };
}  // namespace

TEST_F(PublishRegistrationTest, AddressPublicationSkipsReversePtr) {
  // The service-host alias must publish forward records only: with flags 0
  // the daemon rejects the competing reverse PTR with ERR_COLLISION, the
  // group resets, and the poll loop quits before registration.
  const auto ret = platf::publish::add_service_addresses(
    platf::publish::group,
    "myhost-sunshine.local",
    {{1, boost::asio::ip::make_address("192.168.1.10")}}
  );
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(g_address_calls.size(), 1U);
  ASSERT_NE(g_address_calls[0].flags & avahi::PUBLISH_NO_REVERSE, 0);
  ASSERT_EQ(g_quit_count, 0);
}

TEST_F(PublishRegistrationTest, AddressPublicationUsesOwningInterface) {
  // Lenient stub: isolate the interface finding from the flag finding (F1).
  avahi::entry_group_add_address = FakeEntryGroupAddAddressLenient;

  // A LAN address and a Docker-bridge address must each be published on
  // their owning interface, including distinct link-local IPv6 addresses:
  // publishing everything with IF_UNSPEC would hand LAN clients the
  // bridge address (and vice versa) under the same SRV target.
  const auto ret = platf::publish::add_service_addresses(
    platf::publish::group,
    "myhost-sunshine.local",
    {{1, boost::asio::ip::make_address("192.168.1.10")},
     {2, boost::asio::ip::make_address("172.17.0.1")},
     {1, boost::asio::ip::make_address("fe80::1")},
     {2, boost::asio::ip::make_address("fe80::2")}}
  );
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(g_address_calls.size(), 4U);
  ASSERT_EQ(g_address_calls[0].interface, 1);
  ASSERT_EQ(g_address_calls[1].interface, 2);
  ASSERT_EQ(g_address_calls[2].interface, 1);
  ASSERT_EQ(g_address_calls[3].interface, 2);
  ASSERT_EQ(g_address_calls[0].address, platf::publish::to_avahi_address(boost::asio::ip::make_address("192.168.1.10")));
  ASSERT_EQ(g_address_calls[1].address, platf::publish::to_avahi_address(boost::asio::ip::make_address("172.17.0.1")));
  ASSERT_EQ(g_address_calls[2].address, platf::publish::to_avahi_address(boost::asio::ip::make_address("fe80::1")));
  ASSERT_EQ(g_address_calls[3].address, platf::publish::to_avahi_address(boost::asio::ip::make_address("fe80::2")));
}

TEST_F(PublishRegistrationTest, RefreshRepublishesOnAddressChange) {
  // Lenient stub: isolate the refresh finding from the flag finding (F1).
  avahi::entry_group_add_address = FakeEntryGroupAddAddressLenient;

  // Register .10, change the snapshot to .20, deliver the change
  // notification: the group must be reset (withdrawing .10) and the new
  // snapshot committed, with no stale .10 record and the poll loop alive.
  config::sunshine.bind_address = "192.168.1.10";
  config::sunshine.address_family = "both";

  platf::publish::create_services(reinterpret_cast<avahi::Client *>(0x2000));
  ASSERT_EQ(g_commit_count, 1);
  ASSERT_EQ(g_quit_count, 0);
  ASSERT_EQ(g_address_calls.size(), 1U);
  ASSERT_EQ(g_address_calls[0].address, platf::publish::to_avahi_address(boost::asio::ip::make_address("192.168.1.10")));

  config::sunshine.bind_address = "192.168.1.20";
  platf::publish::refresh_service_addresses();

  ASSERT_EQ(g_reset_count, 1);
  ASSERT_EQ(g_commit_count, 2);
  ASSERT_EQ(g_quit_count, 0);
  ASSERT_EQ(g_address_calls.size(), 2U);
  ASSERT_EQ(g_address_calls[1].address, platf::publish::to_avahi_address(boost::asio::ip::make_address("192.168.1.20")));

  // A second refresh with an unchanged snapshot is a no-op: no churn.
  platf::publish::refresh_service_addresses();
  ASSERT_EQ(g_reset_count, 1);
  ASSERT_EQ(g_commit_count, 2);
}

TEST_F(PublishRegistrationTest, RefreshCollisionRetryDoesNotQuitPollLoop) {
  // A service collision during a refresh retry must not quit the poll
  // loop: the retry inherits the refresh's no-quit-on-failure behavior,
  // and the service is re-registered under a new host name.
  avahi::entry_group_add_address = FakeEntryGroupAddAddressLenient;

  config::sunshine.bind_address = "192.168.1.10";
  config::sunshine.address_family = "both";

  platf::publish::create_services(reinterpret_cast<avahi::Client *>(0x2000));
  ASSERT_EQ(g_commit_count, 1);
  ASSERT_EQ(g_service_hosts.size(), 1U);

  config::sunshine.bind_address = "192.168.1.20";
  g_add_service_collide_once = true;
  platf::publish::refresh_service_addresses();

  ASSERT_EQ(g_quit_count, 0);
  ASSERT_EQ(g_commit_count, 2);
  // Three service registrations: the initial one, the refresh attempt that
  // collided, and the retry under the regenerated host name.
  ASSERT_EQ(g_service_hosts.size(), 3U);
  ASSERT_EQ(g_service_hosts[1], g_service_hosts[0]);
  ASSERT_NE(g_service_hosts[2], g_service_hosts[0]);
  ASSERT_EQ(platf::publish::host_name_collision_count, 1U);
}

#endif  // defined(__linux__)
