/**
 * @file src/platform/linux/misc.h
 * @brief Miscellaneous declarations for Linux.
 */
#pragma once

// standard includes
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <thread>
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

#ifdef SUNSHINE_BUILD_PORTAL
namespace portal {
  /**
   * @brief Cancel any pending DBus requests gracefully.
   */
  void cancel_pending_requests();

  /**
   * @brief Check if a Portal restore token already exists on disk.
   *
   * @return True if a saved token was found.
   */
  bool has_saved_token();

  /**
   * @brief Check if the Portal service is reachable via simple DBus ping with 2s timeout.
   *
   * @return True if the Portal is reachable.
   */
  bool is_portal_service_reachable();
}  // namespace portal
#endif

namespace platf {
  /**
   * @brief Remove environment variables that can load untrusted GUI modules.
   *
   * Loader paths and module lists used by Qt, GTK, GIO, and GdkPixbuf are removed before any of those libraries
   * can initialize. The caller determines whether privileged execution requires the policy.
   *
   * @return `true` when the environment is safe to use; `false` if a variable could not be removed.
   */
  bool sanitize_process_environment();

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

  /**
   * @brief Generic frame pacing logic for Linux capture methods.
   *
   * Advances the frame pacing timeline and re-anchors it if the capture thread falls behind.
   *
   * @param next_frame Time point that the next frame will be targeted against.
   * @param delay Delay interval used to pace next frame.
   * @param logger The sleep_overshoot_logger for tracking discontinuities.
   */
  inline void handle_pacing(std::chrono::steady_clock::time_point &next_frame, std::chrono::nanoseconds delay, auto &logger) {
    auto now = std::chrono::steady_clock::now();

    if (next_frame > now) {
      std::this_thread::sleep_until(next_frame);
      logger.first_point(next_frame);
      logger.second_point_now_and_log();
    }

    next_frame += delay;
    if (next_frame < now) {  // some major slowdown happened; we couldn't keep up
      next_frame = now + delay;
    }
  }

#if defined(__linux__) && defined(SUNSHINE_BUILD_DRM)
  namespace kms {
    /**
     * @brief Force-construct the DRM privileged worker thread while the calling
     *        thread still holds CAP_SYS_ADMIN in CAP_PERMITTED.
     *
     * This must be called before dropping CAP_SYS_ADMIN from the calling thread,
     * since newly-created threads inherit its capability sets.
     */
    void ensure_privileged_drm_worker_started();

    /**
     * @brief Drop the DRM worker thread's privileges.
     */
    void drop_drm_worker_privileges();

    /**
     * @brief Open a DRM card file descriptor using the privileged DRM worker.
     *
     * @param path Path to the DRM card node.
     * @return A file descriptor on success, or `-1` on failure.
     */
    int privileged_open_drm_card_fd(const char *path);
  }  // namespace kms
#endif
}  // namespace platf
