/**
 * @file src/display_prep.h
 * @brief Commands that prepare a capture output before display configuration.
 */
#pragma once

// standard includes
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// local includes
#include "config.h"

namespace rtsp_stream {
  struct launch_session_t;
}

namespace display_prep {
  /** @brief Lifecycle state of the configured display preparation commands. */
  enum class state_e {
    idle,  ///< No preparation is active.
    prepared,  ///< One or more launches own the prepared output.
    restoring,  ///< Waiting for display restoration before running Undo.
    failed  ///< An Undo failed; another Do must not run automatically.
  };

  /** @brief External operations required by the manager. */
  class backend_t {
  public:
    /** @brief Destroy the backend. */
    virtual ~backend_t() = default;
    /**
     * @brief Execute Do commands for the first client.
     * @param session Launch parameters exposed to the commands.
     * @return True if all commands succeeded.
     */
    virtual bool prepare(const rtsp_stream::launch_session_t &session) = 0;
    /**
     * @brief Execute Undo commands in reverse order.
     * @return True if all commands succeeded.
     */
    virtual bool undo() = 0;
    /**
     * @brief Restore display settings.
     * @param on_reverted Receives true after restoration or false if it was abandoned.
     */
    virtual void revert_display(std::function<void(bool)> on_reverted) = 0;
  };

  class manager_t;

  /** @brief Ownership token carried from the launch request to capture teardown. */
  class lease_t {
  public:
    /** @brief Release the lease if nobody did so explicitly. */
    ~lease_t();
    lease_t(const lease_t &) = delete;
    lease_t &operator=(const lease_t &) = delete;

    /**
     * @brief Mark the lease as owned by a running capture session.
     * @return False if the lease was already released.
     */
    bool start();
    /** @brief Release the lease only if no capture session started with it. */
    void expire();
    /** @brief Release the lease unconditionally. */
    void finish();

  private:
    friend class manager_t;

    /** @brief Phase of the lease. */
    enum class phase_e {
      pending,  ///< Waiting for the RTSP handshake.
      active,  ///< Owned by a capture session.
      released  ///< No longer counted by the manager.
    };

    /**
     * @brief Construct an uncounted lease before external preparation begins.
     * @param manager Manager that counts this lease.
     */
    explicit lease_t(std::shared_ptr<manager_t> manager);

    std::shared_ptr<manager_t> manager_;  ///< Keeps the manager alive until release.
    phase_e phase_ {phase_e::released};  ///< Guarded by the manager mutex.
  };

  /** @brief Runs Do before the first lease and Undo after the last one. */
  class manager_t: public std::enable_shared_from_this<manager_t> {
  public:
    /**
     * @brief Construct a manager.
     * @param backend Operations used to run commands and restore displays.
     */
    explicit manager_t(std::unique_ptr<backend_t> backend);

    /**
     * @brief Prepare the output for the first launch or share an existing preparation.
     * @param session Launch parameters passed to the backend.
     * @return A lease, or nullptr on failure (see error()).
     */
    std::shared_ptr<lease_t> acquire(const rtsp_stream::launch_session_t &session);

    /**
     * @brief Get the lifecycle state.
     * @return Current state.
     */
    state_e state() const;

    /**
     * @brief Get the reason for the last failure.
     * @return Human-readable message.
     */
    std::string error() const;

  private:
    friend class lease_t;

    /**
     * @brief Release a lease and restore the display after the last one.
     * @param lease Lease to release.
     * @param pending_only Only release the lease if it has not started.
     */
    void release(lease_t &lease, bool pending_only);

    /**
     * @brief Run Undo on successful restoration, or retain failure for manual recovery.
     * @param restored Whether display settings were actually restored.
     */
    void finish_restore(bool restored);

    /**
     * @brief Run Undo, converting exceptions into failure.
     * @return True if Undo succeeded.
     */
    bool undo_safely();

    mutable std::mutex mutex_;  ///< Guards state, lease phases and commands.
    std::condition_variable restored_;  ///< Signalled when restoration finishes.
    std::unique_ptr<backend_t> backend_;  ///< Runs commands and display restoration.
    int leases_ {0};  ///< Number of unreleased leases.
    state_e state_ {state_e::idle};  ///< Current lifecycle state.
    std::string error_;  ///< Last failure reason.
  };

  /**
   * @brief Create the backend that runs configured commands.
   * @param commands Ordered Do/Undo command pairs.
   * @return Backend using Sunshine's process launcher.
   */
  std::unique_ptr<backend_t> make_command_backend(std::vector<config::prep_cmd_t> commands);

  /**
   * @brief Acquire a lease before display configuration if commands are configured.
   * @param session Launch parameters passed to the commands.
   * @return A lease, or nullptr if no commands are configured.
   * @throws std::runtime_error If preparation failed.
   */
  std::shared_ptr<lease_t> prepare(const rtsp_stream::launch_session_t &session);

  /** @brief Return whether active leases or restoration own the display state. */
  bool owns_display_restoration();
}  // namespace display_prep
