//
// Created by loki on 12/27/19.
//

#ifndef SUNSHINE_NETWORK_H
#define SUNSHINE_NETWORK_H

#include <tuple>
namespace net {
enum net_e : int {
  PC,
  LAN,
  WAN
};

net_e from_enum_string(const std::string_view &view);
std::string_view to_enum_string(net_e net);

net_e from_address(const std::string_view &view);
}

#endif //SUNSHINE_NETWORK_H
