/**
 * @file src/platform/linux/misc.h
 * @brief Miscellaneous declarations for Linux.
 */
#pragma once

// standard includes
#include <fcntl.h>
#include <filesystem>
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

namespace platf {
  /**
   * @brief Open a DRM card node and drop implicit DRM master, if any.
   *
   * Performs `open(path, flags | O_CLOEXEC)` and probes the resulting fd with
   * `DRM_IOCTL_AUTH_MAGIC`. If the kernel implicitly handed us master, calls
   * `drmDropMaster` and re-verifies before returning. Master check/drop failures
   * are logged as warnings but do not fail the call: the caller still receives
   * a usable fd.
   *
   * Callers should use this helper for any `/dev/dri/cardN` open so we never
   * keep implicit master and block compositors from re-acquiring it on VT
   * switches.
   *
   * @param path Path to the DRM card node (e.g. `/dev/dri/card0`).
   * @param flags `open()` flags. `O_CLOEXEC` is always OR-ed in.
   * @return A file descriptor on success, or `-1` if `open()` itself fails.
   */
  int open_drm_card_fd(const std::filesystem::path &path, int flags = O_RDWR);
}  // namespace platf
