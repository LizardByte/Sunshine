/**
 * @file entry_handler.h
 * @brief Declarations for entry handling functions.
 */
#pragma once

// standard includes
#include <atomic>
#include <string_view>

// local includes
#include "thread_pool.h"
#include "thread_safe.h"

/**
 * @brief Launch the Web UI.
 * @examples
 * launch_ui();
 * @examples_end
 */
void launch_ui();

/**
 * @brief Launch the Web UI at a specific endpoint.
 * @examples
 * launch_ui_with_path("/pin");
 * @examples_end
 */
void launch_ui_with_path(std::string path);

/**
 * @brief Functions for handling command line arguments.
 */
namespace args {
  /**
   * @brief Reset the user credentials.
   * @param name The name of the program.
   * @param argc The number of arguments.
   * @param argv The arguments.
   * @examples
   * creds("sunshine", 2, {"new_username", "new_password"});
   * @examples_end
   */
  int creds(const char *name, int argc, char *argv[]);

  /**
   * @brief Print help to stdout, then exit.
   * @param name The name of the program.
   * @examples
   * help("sunshine");
   * @examples_end
   */
  int help(const char *name);

  /**
   * @brief Print the version to stdout, then exit.
   * @examples
   * version();
   * @examples_end
   */
  int version();

#ifdef _WIN32
  /**
   * @brief Restore global NVIDIA control panel settings.
   * If Sunshine was improperly terminated, this function restores
   * the global NVIDIA control panel settings to the undo file left
   * by Sunshine. This function is typically called by the uninstaller.
   * @examples
   * restore_nvprefs_undo();
   * @examples_end
   */
  int restore_nvprefs_undo();
#endif
}  // namespace args

/**
 * @brief Functions for handling the lifetime of Sunshine.
 */
namespace lifetime {
  extern char **argv;
  extern std::atomic_int desired_exit_code;

  /**
   * @brief Terminates Sunshine gracefully with the provided exit code.
   * @param exit_code The exit code to return from main().
   * @param async Specifies whether our termination will be non-blocking.
   */
  void exit_sunshine(int exit_code, bool async);

  /**
   * @brief Breaks into the debugger or terminates Sunshine if no debugger is attached.
   */
  void debug_trap();

  /**
   * @brief Get the argv array passed to main().
   */
  char **get_argv();
}  // namespace lifetime

/**
 * @brief Log the publisher metadata provided from CMake.
 */
void log_publisher_data();

#ifdef _WIN32
/**
 * @brief Check if NVIDIA's GameStream software is running.
 * @return `true` if GameStream is enabled, `false` otherwise.
 */
bool is_gamestream_enabled();

/**
 * @brief Namespace for controlling the Sunshine service model on Windows.
 */
namespace service_ctrl {
  /**
   * @brief Check if the service is running.
   * @examples
   * is_service_running();
   * @examples_end
   */
  bool is_service_running();

  /**
   * @brief Start the service and wait for startup to complete.
   * @examples
   * start_service();
   * @examples_end
   */
  bool start_service();

  /**
   * @brief Wait for the UI to be ready after Sunshine startup.
   * @examples
   * wait_for_ui_ready();
   * @examples_end
   */
  bool wait_for_ui_ready();
}  // namespace service_ctrl
#endif
