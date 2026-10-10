/**
 * @file src/platform/linux/kms_plane.h
 * @brief Declarations for tracking the DRM plane captured by KMS capture.
 */
#pragma once

// standard includes
#include <chrono>
#include <optional>

namespace platf::kms {
  /**
   * @brief Decides when a captured plane that has lost its framebuffer should be picked again.
   *
   * A compositor may move its picture to another plane while a stream is running, or restart.
   * The plane being captured then has no framebuffer, and capturing it again only repeats the
   * last frame. Once it has stayed empty for the given delay, capture should be reinitialized so
   * a plane is chosen again.
   */
  class empty_plane_timer_t {
  public:
    /**
     * @brief Construct a timer.
     *
     * @param delay How long the plane may have no framebuffer before a reinit is due.
     */
    explicit empty_plane_timer_t(std::chrono::steady_clock::duration delay):
        delay_ {delay} {
    }

    /**
     * @brief Record whether the plane has a framebuffer now.
     *
     * @param has_fb True when the plane currently scans out a framebuffer.
     * @param now The current time.
     * @return True once the plane has had no framebuffer for at least the delay.
     */
    bool update(bool has_fb, std::chrono::steady_clock::time_point now) {
      if (has_fb) {
        empty_since_.reset();
        return false;
      }
      if (!empty_since_) {
        empty_since_ = now;
      }
      return now - *empty_since_ >= delay_;
    }

    /**
     * @brief How long the plane has had no framebuffer.
     *
     * @param now The current time.
     * @return The time since the plane lost its framebuffer, or zero when it has one.
     */
    std::chrono::steady_clock::duration empty_for(std::chrono::steady_clock::time_point now) const {
      return empty_since_ ? now - *empty_since_ : std::chrono::steady_clock::duration::zero();
    }

  private:
    std::chrono::steady_clock::duration delay_;  ///< How long an empty plane is tolerated.
    std::optional<std::chrono::steady_clock::time_point> empty_since_;  ///< When the plane lost its framebuffer.
  };
}  // namespace platf::kms
