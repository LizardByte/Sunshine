/**
 * @file globals.h
 * @brief Application infrastructure ownership and mailbox identifiers.
 */
#pragma once

// local includes
#include "entry_handler.h"
#include "thread_pool.h"

#ifdef _WIN32
  #include "platform/windows/nvprefs/nvprefs_interface.h"
#endif

namespace runtime {
  /**
   * @brief Infrastructure owned by one invocation of the Sunshine application.
   *
   * Subsystems receive individual dependencies from this object. The lifetime
   * controller is destroyed last so a requested restart runs after workers and
   * platform resources have been released.
   */
  class infrastructure_t {
  public:
    /**
     * @brief Construct the application's mailbox and inactive task scheduler.
     * @param argv Original command arguments used when restarting.
     */
    explicit infrastructure_t(char **argv = nullptr);

    /**
     * @brief Stop and join queued work before releasing its infrastructure dependencies.
     */
    ~infrastructure_t();

    /**
     * @brief Obtain the scheduler shared by host input and shutdown tasks.
     * @return Application-owned scheduler.
     */
    thread_pool_util::ThreadPool &scheduler();

    /**
     * @brief Obtain the host mailbox shared by packet and lifecycle workers.
     * @return Shared mailbox handle.
     */
    safe::mail_t mailbox() const;

    /**
     * @brief Obtain the cursor preference shared by input and capture.
     * @return Host cursor preference.
     */
    bool &cursor();

    /**
     * @brief Obtain the controller for this application's shutdown and restart.
     * @return Application lifetime controller.
     */
    lifetime::controller_t &lifetime();

#ifdef _WIN32
    /**
     * @brief Obtain the owner of NVIDIA control-panel preference modifications.
     * @return Application-owned NVIDIA preferences.
     */
    nvprefs::nvprefs_interface &preferences();
#endif

  private:
    lifetime::controller_t lifetime_;  ///< Destroyed after all dependent infrastructure.
    thread_pool_util::ThreadPool scheduler_;  ///< Host task scheduler.
    safe::mail_t mailbox_;  ///< Host communication bus.
    bool cursor_ = true;  ///< Shared cursor visibility preference.
#ifdef _WIN32
    nvprefs::nvprefs_interface preferences_;  ///< NVIDIA preference and recovery owner.
#endif
  };
}  // namespace runtime

/**
 * @brief Handles process-wide communication.
 */
namespace mail {
/**
 * @def MAIL(x)
 * @brief Macro for MAIL.
 */
#define MAIL(x) \
  constexpr auto x = std::string_view { \
    #x \
  }

  // Global mail
  MAIL(shutdown);  ///< Shutdown.
  MAIL(broadcast_shutdown);  ///< Broadcast shutdown.
  MAIL(video_packets);  ///< Video packets.
  MAIL(audio_packets);  ///< Audio packets.
  MAIL(switch_display);  ///< Switch display.

  // Local mail
  MAIL(touch_port);  ///< Touch port.
  MAIL(idr);  ///< IDR.
  MAIL(invalidate_ref_frames);  ///< Invalidate ref frames.
  MAIL(gamepad_feedback);  ///< Gamepad feedback.
  MAIL(hdr);  ///< HDR.
#undef MAIL

}  // namespace mail
