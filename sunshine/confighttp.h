//
// Created by loki on 6/3/19.
//

#ifndef SUNSHINE_CONFIGHTTP_H
#define SUNSHINE_CONFIGHTTP_H

#include <functional>
#include <string>

#include "thread_safe.h"

#define WEB_DIR SUNSHINE_ASSETS_DIR "/web/"


namespace confighttp {
void start(std::shared_ptr<safe::signal_t> shutdown_event);
}

#endif //SUNSHINE_CONFIGHTTP_H
