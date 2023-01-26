#pragma once

#include "thread_safe.h"
#include <string>

namespace nvhttp {
constexpr auto PORT_HTTP  = 0;
constexpr auto PORT_HTTPS = -5;

void start();
bool pin(std::string pin);
void erase_all_clients();
} // namespace nvhttp
