//
// Created by loki on 12/22/19.
//

#ifndef SUNSHINE_MAIN_H
#define SUNSHINE_MAIN_H

#include <boost/log/common.hpp>
#include "thread_pool.h"

extern util::ThreadPool task_pool;
extern bool display_cursor;

extern boost::log::sources::severity_logger<int> verbose;
extern boost::log::sources::severity_logger<int> debug;
extern boost::log::sources::severity_logger<int> info;
extern boost::log::sources::severity_logger<int> warning;
extern boost::log::sources::severity_logger<int> error;
extern boost::log::sources::severity_logger<int> fatal;

void log_flush();
#endif //SUNSHINE_MAIN_H
