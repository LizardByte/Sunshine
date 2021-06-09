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
void start(std::shared_ptr<safe::signal_t> shutdown_event);
bool pin(std::string pin);
} // namespace nvhttp

#endif //SUNSHINE_NVHTTP_H
