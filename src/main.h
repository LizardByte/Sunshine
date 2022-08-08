//
// Created by loki on 12/22/19.
//

#ifndef SUNSHINE_MAIN_H
#define SUNSHINE_MAIN_H

#include <string_view>

#include "thread_pool.h"
#include "thread_safe.h"

#include <boost/log/common.hpp>

extern util::ThreadPool task_pool;
extern bool display_cursor;

extern boost::log::sources::severity_logger<int> verbose;
extern boost::log::sources::severity_logger<int> debug;
extern boost::log::sources::severity_logger<int> info;
extern boost::log::sources::severity_logger<int> warning;
extern boost::log::sources::severity_logger<int> error;
extern boost::log::sources::severity_logger<int> fatal;

void log_flush();

void print_help(const char *name);

std::string read_file(const char *path);
int write_file(const char *path, const std::string_view &contents);

std::uint16_t map_port(int port);

namespace mail {
#define MAIL(x) \
  constexpr auto x = std::string_view { #x }

extern safe::mail_t man;

// Global mail
MAIL(shutdown);
MAIL(broadcast_shutdown);

MAIL(video_packets);
MAIL(audio_packets);

MAIL(switch_display);

// Local mail
MAIL(touch_port);
MAIL(idr);
MAIL(rumble);
#undef MAIL
} // namespace mail


#endif //SUNSHINE_MAIN_H
