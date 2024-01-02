#pragma once

// standard includes
#include <mutex>

// local includes
#include "settings.h"

namespace display_device {

  /**
   * @brief A singleton class for managing the display device configuration for the whole Sunshine session.
   *
   * This class is meant to be an entry point for applying the configuration and reverting it later
   * from within the various places in the Sunshine's source code.
   *
   * It is similar to settings_t and is more or less a wrapper around it.
   * However, this class ensures thread-safe usage for the methods and additionally
   * performs automatic cleanups.
   *
   * @note A lazy-evaluated, correctly-destroyed, thread-safe singleton pattern is used here (https://stackoverflow.com/a/1008289).
   */
  class session_t {
  public:
    /**
     * @brief A class that uses RAII to perform cleanup when it's destroyed.
     * @note The deinit_t usage pattern is used here instead of the session_t destructor
     *       to expedite the cleanup process in case of Sunshine termination.
     * @see session_t::init()
     */
    class deinit_t {
    public:
      /**
       * @brief A destructor that restores (or tries to) the initial state.
       */
      virtual ~deinit_t();
    };

    /**
     * @brief Get the singleton instance.
     * @returns Singleton instance for the class.
     *
     * EXAMPLES:
     * ```cpp
     * session_t& session { session_t::get() };
     * ```
     */
    static session_t &
    get();

    /**
     * @brief Initialize the singleton and perform the initial state recovery (if needed).
     * @returns A deinit_t instance that performs cleanup when destroyed.
     * @see deinit_t
     *
     * EXAMPLES:
     * ```cpp
     * const auto session_guard { session_t::init() };
     * ```
     */
    static std::unique_ptr<deinit_t>
    init();

    /**
     * @brief Configure the display device based on the user configuration and the session information.
     *
     * Upon failing to completely apply configuration, the applied settings will be reverted.
     * Or, in some cases, we will keep retrying even when the stream has already started as there
     * is no possibility to apply settings before the stream start.
     *
     * @param config User's video related configuration.
     * @param session Session information.
     * @note There is no return value as we still want to continue with the stream, so that
     *       users can do something about it once they are connected. Otherwise, we might
     *       prevent users from logging in at all...
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * session_t::get().configure_display(video_config, *launch_session);
     * ```
     */
    void
    configure_display(const config::video_t &config, const rtsp_stream::launch_session_t &session);

    /**
     * @brief Revert the display configuration and restore the previous state.
     * @note This method automatically loads the persistence (if any) from the previous Sunshine session.
     * @note In case the state could not be restored, it will be retried again in X seconds
     *       (repeating indefinitely until success or until persistence is reset).
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * const auto result = session_t::get().configure_display(video_config, *launch_session);
     * if (result) {
     *   // Wait for some time
     *   session_t::get().restore_state();
     * }
     * ```
     */
    void
    restore_state();

    /**
     * @brief Reset the persistence and currently held initial display state.
     *
     * This is normally used to get out of the "broken" state where the algorithm wants
     * to restore the initial display state and refuses start the stream in most cases.
     *
     * This could happen if the display is no longer available or the hardware was changed
     * and the device ids no longer match.
     *
     * The user then accepts that Sunshine is not able to restore the state and "agrees" to
     * do it manually.
     *
     * @note This also stops the retry timer.
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * const auto result = session_t::get().configure_display(video_config, *launch_session);
     * if (!result) {
     *   // Wait for user to decide what to do
     *   const bool user_wants_reset { true };
     *   if (user_wants_reset) {
     *     session_t::get().reset_persistence();
     *   }
     * }
     * ```
     */
    void
    reset_persistence();

    /**
     * @brief A deleted copy constructor for singleton pattern.
     * @note Public to ensure better error message.
     */
    session_t(session_t const &) = delete;

    /**
     * @brief A deleted assignment operator for singleton pattern.
     * @note Public to ensure better error message.
     */
    void
    operator=(session_t const &) = delete;

  private:
    /**
     * @brief A class for retrying to set/reset state.
     *
     * This timer class spins a thread which is mostly sleeping all the time, but can be
     * configured to wake up every X seconds.
     *
     * It is tightly synchronized with the session_t class via a shared mutex to ensure
     * that stupid race conditions do not happen where we successfully apply settings
     * for them to be reset by the timer thread immediately.
     */
    class StateRetryTimer;

    /**
     * @brief A private constructor to ensure the singleton pattern.
     * @note Cannot be defaulted in declaration because of forward declared StateRetryTimer.
     */
    explicit session_t();

    /**
     * @brief An implementation of `restore_state` without a mutex lock.
     * @see restore_state for the description.
     */
    void
    restore_state_impl();

    settings_t settings; /**< A class for managing display device settings. */
    std::mutex mutex; /**< A mutex for ensuring thread-safety. */

    /**
     * @brief An instance of StateRetryTimer.
     * @warning MUST BE declared after the settings and mutex members to ensure proper destruction order!.
     */
    std::unique_ptr<StateRetryTimer> timer;
  };

}  // namespace display_device
