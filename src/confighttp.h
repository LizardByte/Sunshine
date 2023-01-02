// Created by loki on 6/3/19.

#ifndef SUNSHINE_CONFIGHTTP_H
#define SUNSHINE_CONFIGHTTP_H

#include <functional>
#include <string>

#include "thread_safe.h"

#define WEB_DIR SUNSHINE_ASSETS_DIR "/web/"


namespace confighttp {
constexpr auto PORT_HTTP = 1;
void start();

enum sse_event_type {
  REQUEST_PIN,
  NEW_LAUNCH_SESSION,
};
void sendSSEEvent(sse_event_type type);

} // namespace confighttp

#endif // SUNSHINE_CONFIGHTTP_H
