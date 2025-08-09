/**
 * @file src/update.h
 * @brief Declarations for update checking, notification, and command execution.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <string>

namespace update {
  struct state_t {
    std::string last_notified_version;  // Version string last notified to user
    std::string last_notified_url;  // Release page URL last notified to user
    bool last_notified_is_prerelease {false};  // Whether last notification was for a prerelease
    std::string last_update_command_version;  // Version string for which update command already executed
    std::string available_version;  // Latest fetched remote stable version
    std::string available_release_url;  // Release page URL for latest stable
    std::string available_release_name;  // Release name for latest stable
    std::string available_release_body;  // Release body/changelog for latest stable
    std::string available_release_published_at;  // Publish date for latest stable
    std::string available_prerelease_version;  // Latest fetched pre-release version (if enabled)
    std::string available_prerelease_url;  // Release page URL for latest prerelease
    std::string available_prerelease_name;  // Release name for latest prerelease
    std::string available_prerelease_body;  // Release body/changelog for latest prerelease
    std::string available_prerelease_published_at;  // Publish date for latest prerelease
    std::chrono::steady_clock::time_point last_check_time;  // Last time we checked
    std::atomic<bool> check_in_progress {false};
  };

  extern state_t state;


  /**
   * @brief Trigger an asynchronous update check.
   * Initiates a check for updates if not already running. If @p force is true, bypasses interval throttling.
   * @param force If `true`, forces the check regardless of throttling.
   * @param allow_auto_execute If `true`, allows automatic execution of the update command if an update is found.
   */
  void trigger_check(bool force, bool allow_auto_execute);


  /**
   * @brief Run the configured update command if allowed.
   * Executes the update command if it is configured and permitted.
   * @return Returns `true` if the update command was executed, otherwise `false`.
   */
  bool run_update_command();


  /**
   * @brief Handle stream start event for update notification.
   * Called when a stream transitions from 0 to 1 clients to schedule a delayed update notification.
   */
  void on_stream_started();


  /**
   * @brief Periodic tick to evaluate update check timing.
   * Called from a task pool or timer to determine if the next update check is due.
   */
  void periodic();


  /**
   * @brief Download GitHub release data for a repository.
   * Fetches release data in JSON format for the specified repository.
   * @param owner The GitHub repository owner.
   * @param repo The GitHub repository name.
   * @param out_json Output parameter for the fetched JSON data.
   * @return Returns `true` on success, otherwise `false`.
   */
  bool download_github_release_data(const std::string &owner, const std::string &repo, std::string &out_json);


  /**
   * @brief Open the last-notified release page.
   * Callback used by tray notifications to open the release page last notified to the user.
   */
  void open_last_notified_release_page();
}  // namespace update
