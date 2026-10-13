/**
 * @file src/platform/linux/publish.h
 * @brief Declarations for publishing services on Linux (Avahi).
 */
#pragma once

// local includes
#include "src/network.h"

namespace avahi {
  /**
   * @brief Enumerates supported proto options.
   */
  enum proto {
    PROTO_INET = 0,  ///< IPv4
    PROTO_INET6 = 1,  ///< IPv6
    PROTO_UNSPEC = -1  ///< Unspecified/all protocol(s)
  };

  /**
   * @brief Avahi protocol/address-family selector type.
   */
  using Protocol = int;
}  // namespace avahi

namespace platf::publish {
  /**
   * @brief Map Sunshine's configured address family to the Avahi protocol selector.
   *
   * Avahi publishes the service for every protocol selected here: passing
   * `PROTO_UNSPEC` makes the daemon advertise both A (IPv4) and AAAA (IPv6)
   * records for the host, even when Sunshine only binds listeners for one
   * family. With `address_family = ipv4` that advertised an IPv6 link-local
   * address nothing was listening on, so IPv6-preferring Moonlight clients
   * picked it and stalled on connect (see #5860). This maps the configured
   * family to the matching selector so the announcement only contains
   * addresses Sunshine actually serves.
   *
   * @param af Address family from the configuration.
   * @return `PROTO_INET` for IPv4-only, `PROTO_UNSPEC` for both families.
   */
  [[nodiscard]] avahi::Protocol protocol_for_address_family(net::af_e af);
}  // namespace platf::publish
