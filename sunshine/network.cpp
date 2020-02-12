//
// Created by loki on 12/27/19.
//

#include <algorithm>
#include "network.h"
#include "utility.h"

namespace net {
using namespace std::literals;
// In the format "xxx.xxx.xxx.xxx/x"
std::pair<std::uint32_t, std::uint32_t> ip_block(const std::string_view &ip);

std::vector<std::pair<std::uint32_t, std::uint32_t>> pc_ips {
  ip_block("127.0.0.1/32"sv)
};
std::vector<std::tuple<std::uint32_t, std::uint32_t>> lan_ips {
  ip_block("192.168.0.0/16"sv),
  ip_block("172.16.0.0/12"),
  ip_block("10.0.0.0/8"sv)
};

std::uint32_t ip(const std::string_view &ip_str) {
  auto begin = std::begin(ip_str);
  auto end = std::end(ip_str);
  auto temp_end = std::find(begin, end, '.');

  std::uint32_t ip = 0;
  auto shift = 24;
  while(temp_end != end) {
    ip += (util::from_chars(begin, temp_end) << shift);
    shift -= 8;

    begin = temp_end + 1;
    temp_end = std::find(begin, end, '.');
  }

  ip += util::from_chars(begin, end);

  return ip;
}

// In the format "xxx.xxx.xxx.xxx/x"
std::pair<std::uint32_t, std::uint32_t> ip_block(const std::string_view &ip_str) {
  auto begin = std::begin(ip_str);
  auto end = std::find(begin, std::end(ip_str), '/');

  auto addr = ip({ begin, (std::size_t)(end - begin) });

  auto bits = 32 - util::from_chars(end + 1, std::end(ip_str));

  return { addr, addr + ((1 << bits) - 1) };
}

net_e from_enum_string(const std::string_view &view) {
  if(view == "wan") {
    return WAN;
  }
  if(view == "lan") {
    return LAN;
  }

  return PC;
}
net_e from_address(const std::string_view &view) {
  auto addr = ip(view);

  for(auto [ip_low, ip_high] : pc_ips) {
    if(addr >= ip_low && addr <= ip_high) {
      return PC;
    }
  }

  for(auto [ip_low, ip_high] : lan_ips) {
    if(addr >= ip_low && addr <= ip_high) {
      return LAN;
    }
  }

  return WAN;
}

std::string_view to_enum_string(net_e net) {
  switch(net) {
    case PC:
      return "pc"sv;
    case LAN:
      return "lan"sv;
    case WAN:
      return "wan"sv;
  }

  // avoid warning
  return "wan"sv;
}

host_t host_create(ENetAddress &addr, std::size_t peers, std::uint16_t port) {
  enet_address_set_host(&addr, "0.0.0.0");
  enet_address_set_port(&addr, port);

  return host_t { enet_host_create(AF_INET, &addr, peers, 1, 0, 0) };
}

void free_host(ENetHost *host) {
  std::for_each(host->peers, host->peers + host->peerCount, [](ENetPeer &peer_ref) {
    ENetPeer *peer = &peer_ref;

    if(peer) {
      enet_peer_disconnect_now(peer, 0);
    }
  });

  enet_host_destroy(host);
}
}