#ifndef SUNSHINE_PUBLISH_H
#define SUNSHINE_PUBLISH_H

#include "thread_safe.h"

#define SERVICE_NAME "Sunshine"
#define SERVICE_TYPE "_nvstream._tcp"

namespace publish {
void start(std::shared_ptr<safe::signal_t> shutdown_event);
}

#endif //SUNSHINE_PUBLISH_H
