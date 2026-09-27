/**
 * @file src/platform/macos/misc.h
 * @brief Miscellaneous declarations for macOS platform.
 */
#pragma once

// standard includes
#include <vector>

// local includes
#include "src/platform/permissions.h"

namespace platf {
  /**
   * @brief Decide whether startup should initiate a native permission request.
   *
   * @param permission Permission status and requirement for the active configuration.
   * @param notifications_enabled Whether the system tray uses notifications.
   * @return True when a missing permission should be requested.
   */
  bool should_request_startup_permission(const permission_status_t &permission, bool notifications_enabled);

  /**
   * @brief Request missing permissions needed at startup.
   *
   * @param notifications_enabled Whether the running build shows tray notifications.
   */
  void request_startup_permissions(bool notifications_enabled);

  /**
   * @brief Start and stop an unmuted Core Audio tap to request system audio access.
   *
   * @return True when the temporary tap started successfully.
   */
  bool request_system_audio_permission();

  /**
   * @brief Check whether macOS has granted screen-capture permission.
   *
   * @return True when Sunshine can capture the screen.
   */
  bool is_screen_capture_allowed();
}  // namespace platf

namespace dyn {
  typedef void (*apiproc)();

  /**
   * @brief Load persisted state from its backing store.
   *
   * @param handle Native library or object handle used by the operation.
   * @param funcs Function table populated from the loaded library.
   * @param strict Whether missing functions should be treated as an error.
   * @return 0 when all required symbols are loaded; nonzero when loading fails.
   */
  int load(void *handle, const std::vector<std::tuple<apiproc *, const char *>> &funcs, bool strict = true);
  /**
   * @brief Return the native handle owned by the wrapper.
   *
   * @param libs List of libraries to probe for the requested symbol.
   * @return Native dynamic-library handle, or nullptr when no library can be opened.
   */
  void *handle(const std::vector<const char *> &libs);

}  // namespace dyn
