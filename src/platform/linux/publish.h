/**
 * @file src/platform/linux/publish.h
 * @brief Declarations for publishing services on Linux (Avahi).
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// local includes
#include "src/network.h"

namespace avahi {
  /**
   * @brief Enumerates supported proto options.
   */
  enum class proto : int {
    PROTO_INET = 0,  ///< IPv4
    PROTO_INET6 = 1,  ///< IPv6
    PROTO_UNSPEC = -1  ///< Unspecified/all protocol(s)
  };

  /**
   * @brief Avahi protocol/address-family selector type.
   */
  using Protocol = int;

  /**
   * @brief Address record published for a host name.
   * @details Layout-compatible with Avahi's `AvahiAddress` (see
   * avahi-common/address.h): a protocol selector followed by 16 bytes of
   * address data. An IPv4 address occupies the first 4 bytes in network byte
   * order and the remaining bytes are zero; an IPv6 address occupies all 16
   * bytes.
   */
  struct Address {
    Protocol proto;  ///< Address family of the record.
    std::array<std::uint8_t, 16> data;  ///< Address bytes, see details above.

    /**
     * @brief Compare two address records for equality.
     * @param other Address record to compare against.
     * @return True when the protocol selector and address bytes both match.
     */
    bool operator==(const Address &other) const = default;
  };

  static_assert(sizeof(Address) == sizeof(Protocol) + 16, "avahi::Address must match the AvahiAddress layout");

  /**
   * @brief Flags for publishing functions.
   */
  enum PublishFlags {
    PUBLISH_UNIQUE = 1,  ///< For raw records: The RRset is intended to be unique
    PUBLISH_NO_PROBE = 2,  ///< For raw records: Though the RRset is intended to be unique no probes shall be sent
    PUBLISH_NO_ANNOUNCE = 4,  ///< For raw records: Do not announce this RR to other hosts
    PUBLISH_ALLOW_MULTIPLE = 8,  ///< For raw records: Allow multiple local records of this type, even if they are intended to be unique
    PUBLISH_NO_REVERSE = 16,  ///< For address records: don't create a reverse (PTR) entry
    PUBLISH_NO_COOKIE = 32,  ///< For service records: do not implicitly add the local service cookie to TXT data
    PUBLISH_UPDATE = 64,  ///< Update existing records instead of adding new ones
    PUBLISH_USE_WIDE_AREA = 128,  ///< Register the record using wide area DNS (i.e. unicast DNS update)
    PUBLISH_USE_MULTICAST = 256  ///< Register the record using multicast DNS
  };

  /**
   * @brief Network interface index type used by Avahi.
   */
  using IfIndex = int;

  constexpr auto IF_UNSPEC = -1;  ///< Protocol or platform constant for if unspec.

  struct EntryGroup;
  struct Client;
  struct Poll;
  struct SimplePoll;

  /**
   * @brief Function pointer used to free Avahi-allocated memory.
   */
  using free_fn = void (*)(void *);

  /**
   * @brief Function pointer used to destroy an Avahi client.
   */
  using client_free_fn = void (*)(Client *);

  /**
   * @brief Function pointer used to free an Avahi simple-poll instance.
   */
  using simple_poll_free_fn = void (*)(SimplePoll *);

  /**
   * @brief Function pointer used to request an alternative Avahi service name.
   */
  using alternative_service_name_fn = char *(*)(char *);

  /**
   * @brief Function pointer used to get an Avahi client from an entry group.
   */
  using entry_group_get_client_fn = Client *(*)(EntryGroup *);

  /**
   * @brief Function pointer used to add a service to an Avahi entry group.
   */
  using entry_group_add_service_fn = int (*)(
    EntryGroup *group,
    IfIndex interface,
    Protocol protocol,
    PublishFlags flags,
    const char *name,
    const char *type,
    const char *domain,
    const char *host,
    uint16_t port,
    ...
  );

  /**
   * @brief Function pointer used to add an address record to an Avahi entry group.
   */
  using entry_group_add_address_fn = int (*)(
    EntryGroup *group,
    IfIndex interface,
    Protocol protocol,
    PublishFlags flags,
    const char *name,
    const Address *address
  );

  /**
   * @brief Function pointer used to test whether an Avahi entry group is empty.
   */
  using entry_group_is_empty_fn = int (*)(EntryGroup *);

  /**
   * @brief Function pointer used to reset an Avahi entry group.
   */
  using entry_group_reset_fn = int (*)(EntryGroup *);

  /**
   * @brief Function pointer used to publish an Avahi entry group.
   */
  using entry_group_commit_fn = int (*)(EntryGroup *);

  /**
   * @brief Function pointer used to format Avahi error codes.
   */
  using strerror_fn = char *(*)(int);

  /**
   * @brief Function pointer used to read the last Avahi client error.
   */
  using client_errno_fn = int (*)(Client *);

  /**
   * @brief Function pointer used to stop the Avahi simple-poll loop.
   */
  using simple_poll_quit_fn = void (*)(SimplePoll *);

  /**
   * @brief Function pointer used to get the Avahi simple-poll API.
   */
  using simple_poll_get_fn = Poll *(*)(SimplePoll *);

  extern free_fn free;  ///< Free.
  extern client_free_fn client_free;  ///< Client free.
  extern simple_poll_free_fn simple_poll_free;  ///< Simple poll free.
  extern alternative_service_name_fn alternative_service_name;  ///< Alternative service name.
  extern entry_group_get_client_fn entry_group_get_client;  ///< Entry group get client.
  extern entry_group_add_service_fn entry_group_add_service;  ///< Entry group add service.
  extern entry_group_add_address_fn entry_group_add_address;  ///< Entry group add address.
  extern entry_group_is_empty_fn entry_group_is_empty;  ///< Entry group is empty.
  extern entry_group_reset_fn entry_group_reset;  ///< Entry group reset.
  extern entry_group_commit_fn entry_group_commit;  ///< Entry group commit.
  extern strerror_fn strerror;  ///< Strerror.
  extern client_errno_fn client_errno;  ///< Client errno.
  extern simple_poll_quit_fn simple_poll_quit;  ///< Simple poll quit.
  extern simple_poll_get_fn simple_poll_get;  ///< Simple poll get.
}  // namespace avahi

namespace platf::publish {
  /**
   * @brief Local address and the interface on which it is reachable.
   */
  struct ServedAddress {
    avahi::IfIndex interface;  ///< Owning interface index.
    boost::asio::ip::address address;  ///< Local address on that interface.

    /**
     * @brief Compare two served addresses for equality.
     * @param other Served address to compare against.
     * @return True when the interface index and address both match.
     */
    bool operator==(const ServedAddress &other) const = default;
  };

  /**
   * @brief Release memory allocated by Avahi.
   *
   * @param p Avahi-allocated pointer to release.
   */
  template<class T>
  void free(T *p) {
    avahi::free(p);
  }

  /**
   * @brief Owning pointer for Avahi allocations released with `avahi_free`.
   */
  template<class T>
  using ptr_t = util::safe_ptr<T, free<T>>;
  /**
   * @brief Owning pointer for an Avahi client.
   */
  using client_t = util::dyn_safe_ptr<avahi::Client, &avahi::client_free>;
  /**
   * @brief Owning pointer for an Avahi simple poll loop.
   */
  using poll_t = util::dyn_safe_ptr<avahi::SimplePoll, &avahi::simple_poll_free>;

  /**
   * @brief Build the service-specific host name used as the mDNS SRV target.
   * @details The name must differ from the daemon's shared host name: the
   * daemon publishes both A and AAAA records for its own host name, so
   * pointing the service at it would advertise an IPv6 endpoint an IPv4-only
   * configuration does not serve (see #5860). The label is sanitized from
   * the machine host name (alphanumerics and dashes; spaces become dashes;
   * anything else ends the label), suffixed with `-sunshine` (plus
   * `-<collision_suffix>` after a host-name collision), and qualified with
   * `.local`.
   * @param hostname Machine host name the label is derived from.
   * @param collision_suffix Suffix appended after a host-name collision; 0 for the first attempt.
   * @return Fully qualified service host name, e.g. `myhost-sunshine.local`.
   */
  [[nodiscard]] inline std::string service_host_name(std::string_view hostname, unsigned collision_suffix) {
    std::string label;
    for (const char c : hostname) {
      if (c == ' ') {
        label.push_back('-');
      } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
        label.push_back(c);
      } else {
        break;
      }
    }
    if (label.empty()) {
      label = "sunshine";
    }

    std::string suffix = "-sunshine";
    if (collision_suffix > 0) {
      suffix += std::format("-{}", collision_suffix);
    }

    // A DNS label holds at most 63 characters.
    if (constexpr std::size_t MAX_LABEL = 63; label.size() + suffix.size() > MAX_LABEL) {
      label.resize(MAX_LABEL - suffix.size());
    }

    return label + suffix + ".local";
  }

  /**
   * @brief Select the addresses published for the service host name.
   * @details Keeps only addresses Sunshine serves: the candidate is
   * normalized, dropped when it is unspecified or loopback, dropped when its
   * family falls outside the effective family, and deduplicated by interface
   * plus address, so the same address on two interfaces is published once
   * per interface it is reachable on. In particular, an IPv4-only
   * configuration selects no IPv6 address, so no AAAA endpoint is published
   * for the service host name (see #5860).
   * @param candidates Candidate addresses, e.g. from interface enumeration.
   * @param effective_family Address family Sunshine actually serves.
   * @return Addresses to publish, containing no address outside the effective family.
   */
  [[nodiscard]] inline std::vector<ServedAddress> select_served_addresses(
    const std::vector<ServedAddress> &candidates,
    net::af_e effective_family
  ) {
    std::vector<ServedAddress> selected;
    for (auto candidate : candidates) {
      candidate.address = net::normalize_address(candidate.address);
      if (candidate.address.is_unspecified() || candidate.address.is_loopback()) {
        continue;
      }
      if (effective_family == net::IPV4 && !candidate.address.is_v4()) {
        continue;
      }
      if (std::find(selected.begin(), selected.end(), candidate) == selected.end()) {
        selected.push_back(candidate);
      }
    }
    return selected;
  }

  /**
   * @brief Convert a generic address to the Avahi address-record layout.
   * @details The address is normalized first, so IPv4-mapped IPv6 addresses
   * become plain IPv4 addresses.
   * @param address Address to convert.
   * @return Avahi-compatible address record for `address`.
   */
  [[nodiscard]] inline avahi::Address to_avahi_address(boost::asio::ip::address address) {
    address = net::normalize_address(address);

    avahi::Address result {};
    if (address.is_v4()) {
      result.proto = std::to_underlying(avahi::proto::PROTO_INET);
      const auto bytes = address.to_v4().to_bytes();
      std::ranges::copy(bytes, result.data.begin());
    } else {
      result.proto = std::to_underlying(avahi::proto::PROTO_INET6);
      result.data = address.to_v6().to_bytes();
    }
    return result;
  }

  /**
   * @brief Enumerate the local interface addresses available for publication.
   * @return Interface addresses with their owning interface index, empty when enumeration fails.
   */
  [[nodiscard]] std::vector<ServedAddress> enumerate_interface_addresses();

  /**
   * @brief Determine the addresses published for the service host name.
   * @details An explicit, non-wildcard bind address is published exactly as
   * configured, with the interface index of the interface that owns it
   * (`IF_UNSPEC` when no local interface owns it). A wildcard (or
   * unparsable) bind address publishes the selected local interface
   * addresses.
   * @param effective_family Address family Sunshine actually serves.
   * @return Addresses to publish; empty when none can be determined.
   */
  [[nodiscard]] std::vector<ServedAddress> served_addresses(net::af_e effective_family);

  /**
   * @brief Add address records for the service host name to the entry group.
   *
   * @param entry_group Entry group the address records are added to.
   * @param service_host Service host name the address records are published for.
   * @param addresses Addresses to publish for the service host name.
   * @return 0 when all address records were added; otherwise the Avahi error code from the failed call.
   */
  int add_service_addresses(
    avahi::EntryGroup *entry_group,
    const std::string &service_host,
    const std::vector<ServedAddress> &addresses
  );

  /**
   * @brief Create or update the Avahi service entry group.
   *
   * @param c Avahi client used to allocate and commit the service entry group.
   * @param quit_on_failure Quit the Avahi poll loop when registration fails. Initial
   * registration keeps the existing fail-fast behavior; a refresh triggered by a
   * network change passes false so a transient failure does not kill discovery.
   */
  void create_services(avahi::Client *c, bool quit_on_failure = true);

  /**
   * @brief Rebuild the service records after local interface addresses change.
   * @details Compares the current served addresses against the last published
   * snapshot and, when they differ, resets the entry group (withdrawing the
   * stale records) and republishes. A no-op when the client or group is not
   * yet set up, or when the snapshot is unchanged.
   */
  void refresh_service_addresses();

  /**
   * @brief Subscribe to interface/address changes on the Avahi poll thread.
   * @details Opens a netlink route socket and registers it as an Avahi poll
   * watch, so address/link changes dispatch the refresh on the existing
   * poll thread. Best-effort: returns false when the watch cannot be
   * installed, and discovery continues without refresh. Linux-only:
   * netlink is a Linux-kernel API, so on other platforms this always
   * returns false and no watch is installed; the service there uses the
   * daemon-managed host name (a null SRV target), so the Avahi daemon
   * maintains the address records and no watch is needed.
   * @return True when the watch is installed (or was already installed).
   */
  bool install_address_change_watch();

#ifdef SUNSHINE_TESTS
  // Test seams: registration tests drive the publisher with stubbed Avahi
  // calls, so the publisher state they need to set is visible here. These
  // declarations exist only in the test build.
  extern avahi::EntryGroup *group;  ///< Active Avahi entry group that owns the published service.
  extern poll_t poll;  ///< Avahi poll loop used while the service is published.
  extern client_t client;  ///< Avahi client used to register the Sunshine service.
  extern ptr_t<char> name;  ///< Current service name, updated when Avahi reports a collision.
  extern unsigned host_name_collision_count;  ///< Suffix counter for regenerating the service host name after collisions.
  extern std::optional<std::vector<ServedAddress>> published_addresses;  ///< Committed snapshot; nullopt while withdrawn.
#endif
}  // namespace platf::publish
