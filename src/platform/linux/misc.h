/**
 * @file src/platform/linux/misc.h
 * @brief Miscellaneous declarations for Linux.
 */
#pragma once

// standard includes
#include <unistd.h>
#include <vector>

// local includes
#include "src/utility.h"

#ifndef DOXYGEN
KITTY_USING_MOVE_T(file_t, int, -1, {
  if (el >= 0) {
    close(el);
  }
});
#else
/**
 * @brief Move-only wrapper for a POSIX file descriptor.
 */
class file_t;
#endif

/**
 * @brief Enumerates supported window system options.
 */
enum class window_system_e {
  NONE,  ///< No window system
  X11,  ///< X11
  WAYLAND,  ///< Wayland
};

extern window_system_e window_system;  ///< Window system.

namespace dyn {
  /**
   * @brief Generic GLX procedure pointer returned by the loader.
   */
  typedef void (*apiproc)(void);

  int load(void *handle, const std::vector<std::tuple<apiproc *, const char *>> &funcs, bool strict = true);
  void *handle(const std::vector<const char *> &libs);

}  // namespace dyn
