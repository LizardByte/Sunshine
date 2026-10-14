/**
 * @file src/platform/linux/publish.h
 * @brief Declarations for publishing services on Linux (Avahi).
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstdint>
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
  };

  static_assert(sizeof(Address) == sizeof(Protocol) + 16, "avahi::Address must match the AvahiAddress layout");
}  // namespace avahi

namespace platf::publish {
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
      suffix += "-" + std::to_string(collision_suffix);
    }

    // A DNS label holds at most 63 characters.
    constexpr std::size_t MAX_LABEL = 63;
    if (label.size() + suffix.size() > MAX_LABEL) {
      label.resize(MAX_LABEL - suffix.size());
    }

    return label + suffix + ".local";
  }

  /**
   * @brief Select the addresses published for the service host name.
   * @details Keeps only addresses Sunshine serves: the candidate is
   * normalized, dropped when it is unspecified or loopback, dropped when its
   * family falls outside the effective family, and deduplicated. In
   * particular, an IPv4-only configuration selects no IPv6 address, so no
   * AAAA endpoint is published for the service host name (see #5860).
   * @param candidates Candidate addresses, e.g. from interface enumeration.
   * @param effective_family Address family Sunshine actually serves.
   * @return Addresses to publish, containing no address outside the effective family.
   */
  [[nodiscard]] inline std::vector<boost::asio::ip::address> select_served_addresses(
    std::vector<boost::asio::ip::address> candidates,
    net::af_e effective_family
  ) {
    std::vector<boost::asio::ip::address> selected;
    for (auto candidate : candidates) {
      candidate = net::normalize_address(candidate);
      if (candidate.is_unspecified() || candidate.is_loopback()) {
        continue;
      }
      if (effective_family == net::IPV4 && !candidate.is_v4()) {
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
      std::copy(bytes.begin(), bytes.end(), result.data.begin());
    } else {
      result.proto = std::to_underlying(avahi::proto::PROTO_INET6);
      result.data = address.to_v6().to_bytes();
    }
    return result;
  }

  /**
   * @brief Enumerate the local interface addresses available for publication.
   * @return Interface addresses, empty when enumeration fails.
   */
  [[nodiscard]] std::vector<boost::asio::ip::address> enumerate_interface_addresses();

  /**
   * @brief Determine the addresses published for the service host name.
   * @details An explicit, non-wildcard bind address is published exactly as
   * configured. A wildcard (or unparsable) bind address publishes the
   * selected local interface addresses.
   * @param effective_family Address family Sunshine actually serves.
   * @return Addresses to publish; empty when none can be determined.
   */
  [[nodiscard]] std::vector<boost::asio::ip::address> served_addresses(net::af_e effective_family);
}  // namespace platf::publish
