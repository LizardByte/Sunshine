/**
 * @file src/main.h
 * @brief Main header file for the Sunshine application.
 */

// macros
#pragma once

// standard includes
#include <filesystem>
#include <string_view>

// local includes
#include "thread_pool.h"
#include "thread_safe.h"

#ifdef _WIN32
  // Declare global singleton used for NVIDIA control panel modifications
  #include "platform/windows/nvprefs/nvprefs_interface.h"
extern nvprefs::nvprefs_interface nvprefs_instance;
#endif

extern thread_pool_util::ThreadPool task_pool;
extern bool display_cursor;

// functions
int
main(int argc, char *argv[]);
std::string
read_file(const char *path);
int
write_file(const char *path, const std::string_view &contents);
std::uint16_t
map_port(int port);
void
launch_ui();
void
launch_ui_with_path(std::string path);

// namespaces
namespace mail {
#define MAIL(x)                         \
  constexpr auto x = std::string_view { \
    #x                                  \
  }

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
  MAIL(invalidate_ref_frames);
  MAIL(gamepad_feedback);
  MAIL(hdr);
#undef MAIL

}  // namespace mail

namespace lifetime {
  void
  exit_sunshine(int exit_code, bool async);
  char **
  get_argv();
}  // namespace lifetime

#ifdef _WIN32
namespace service_ctrl {
  bool
  is_service_running();

  bool
  start_service();

  bool
  wait_for_ui_ready();
}  // namespace service_ctrl
#endif
