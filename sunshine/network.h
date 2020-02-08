//
// Created by loki on 12/27/19.
//

#ifndef SUNSHINE_NETWORK_H
#define SUNSHINE_NETWORK_H

#include <tuple>

#include <enet/enet.h>

#include "utility.h"

namespace net {
void free_host(ENetHost *host);

using host_t = util::safe_ptr<ENetHost, free_host>;
using peer_t = ENetPeer*;
using packet_t = util::safe_ptr<ENetPacket, enet_packet_destroy>;

enum net_e : int {
  PC,
  LAN,
  WAN
};

net_e from_enum_string(const std::string_view &view);
std::string_view to_enum_string(net_e net);

net_e from_address(const std::string_view &view);

host_t host_create(ENetAddress &addr, std::size_t peers, std::uint16_t port);
}

#endif //SUNSHINE_NETWORK_H
