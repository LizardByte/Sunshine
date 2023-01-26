#pragma once

#include <functional>
#include <string>

#include "thread_safe.h"

#define WEB_DIR SUNSHINE_ASSETS_DIR "/web/"


namespace confighttp {
constexpr auto PORT_HTTPS = 1;
void start();
} // namespace confighttp
