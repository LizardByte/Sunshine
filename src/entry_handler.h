/**
 * @file entry_handler.h
 * @brief Header file for entry point functions.
 */
#pragma once

// standard includes
#include <atomic>
#include <string_view>

// local includes
#include "thread_pool.h"
#include "thread_safe.h"

// functions
void
launch_ui();
void
launch_ui_with_path(std::string path);

#ifdef _WIN32
// windows only functions
bool
is_gamestream_enabled();
#endif

namespace args {
  int
  creds(const char *name, int argc, char *argv[]);
  int
  help(const char *name, int argc, char *argv[]);
  int
  version(const char *name, int argc, char *argv[]);
#ifdef _WIN32
  int
  restore_nvprefs_undo(const char *name, int argc, char *argv[]);
#endif
}  // namespace args

namespace lifetime {
  extern char **argv;
  extern std::atomic_int desired_exit_code;
  void
  exit_sunshine(int exit_code, bool async);
  void
  debug_trap();
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
