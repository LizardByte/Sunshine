/**
 * @file src/network.h
 * @brief todo
 */
#pragma once

#include <tuple>

#include <boost/asio.hpp>

#include <boost/asio.hpp>

#include <enet/enet.h>

#include "utility.h"

namespace net {
  void
  free_host(ENetHost *host);

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


net_e from_enum_string(const std::string_view &view);
std::string_view to_enum_string(net_e net);

net_e from_address(const std::string_view &view);

af_e af_from_enum_string(const std::string_view &view);
std::string_view af_to_any_address_string(af_e af);
std::string_view af_to_enum_string(af_e af);

host_t host_create(af_e af, ENetAddress &addr, std::size_t peers, std::uint16_t port);

std::string addr_to_normalized_string(boost::asio::ip::address address);
std::string addr_to_url_escaped_string(boost::asio::ip::address address);
}  // namespace net
