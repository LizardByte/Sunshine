//
// Created by loki on 6/3/19.
//

#ifndef SUNSHINE_NVHTTP_H
#define SUNSHINE_NVHTTP_H

#include "thread_safe.h"
#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <functional>
#include <string>

namespace nvhttp {
constexpr auto PORT_HTTP  = 47989;
constexpr auto PORT_HTTPS = 47984;

void start();
bool pin(std::string pin);
} // namespace nvhttp

#endif //SUNSHINE_NVHTTP_H
