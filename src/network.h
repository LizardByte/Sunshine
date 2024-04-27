/**
 * @file src/network.h
 * @brief todo
 */
#pragma once

#include <tuple>
#include <utility>

#include <boost/asio.hpp>

#include <enet/enet.h>

#include "utility.h"

namespace net {
  void
  free_host(ENetHost *host);

  std::uint16_t
  map_port(int port);

  using host_t = util::safe_ptr<ENetHost, free_host>;
  using peer_t = ENetPeer *;
  using packet_t = util::safe_ptr<ENetPacket, enet_packet_destroy>;

  enum net_e : int {
    PC,
    LAN,
    WAN
  };

  enum af_e : int {
    IPV4,
    BOTH
  };

  net_e
  from_enum_string(const std::string_view &view);
  std::string_view
  to_enum_string(net_e net);

  net_e
  from_address(const std::string_view &view);

  host_t
  host_create(af_e af, ENetAddress &addr, std::size_t peers, std::uint16_t port);

  /**
   * @brief Returns the `af_e` enum value for the `address_family` config option value.
   * @param view The config option value.
   * @return The `af_e` enum value.
   */
  af_e
  af_from_enum_string(const std::string_view &view);

  /**
   * @brief Returns the wildcard binding address for a given address family.
   * @param af Address family.
   * @return Normalized address.
   */
  std::string_view
  af_to_any_address_string(af_e af);

  /**
   * @brief Converts an address to a normalized form.
   * @details Normalization converts IPv4-mapped IPv6 addresses into IPv4 addresses.
   * @param address The address to normalize.
   * @return Normalized address.
   */
  boost::asio::ip::address
  normalize_address(boost::asio::ip::address address);

  /**
   * @brief Returns the given address in normalized string form.
   * @details Normalization converts IPv4-mapped IPv6 addresses into IPv4 addresses.
   * @param address The address to normalize.
   * @return Normalized address in string form.
   */
  std::string
  addr_to_normalized_string(boost::asio::ip::address address);

  /**
   * @brief Returns the given address in a normalized form for in the host portion of a URL.
   * @details Normalization converts IPv4-mapped IPv6 addresses into IPv4 addresses.
   * @param address The address to normalize and escape.
   * @return Normalized address in URL-escaped string.
   */
  std::string
  addr_to_url_escaped_string(boost::asio::ip::address address);

  /**
   * @brief Returns the encryption mode for the given remote endpoint address.
   * @param address The address used to look up the desired encryption mode.
   * @return The WAN or LAN encryption mode, based on the provided address.
   */
  int
  encryption_mode_for_address(boost::asio::ip::address address);
}  // namespace net
