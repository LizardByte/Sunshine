/**
 * @file src/update_checker.h
 * @brief Declarations for the update checker functionality.
 */
#pragma once

// standard includes
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace update_checker {
  /**
   * @brief Structure representing a Sunshine version.
   */
  struct Version {
    std::string version;
    std::string name;
    std::string tag_name;
    std::string html_url;
    std::string body;
    bool prerelease;

    /**
     * @brief Compare if this version is greater than another.
     */
    bool isGreater(const Version &other) const;
  };

  /**
   * @brief Structure representing update information.
   */
  struct UpdateInfo {
    bool has_stable_update;
    bool has_prerelease_update;
    Version stable_version;
    Version prerelease_version;
    Version current_version;
  };

  /**
   * @brief Callback function type for update notifications.
   */
  using UpdateCallback = std::function<void(const UpdateInfo &)>;

  /**
   * @brief Initialize the update checker.
   * @param callback Function to call when updates are found
   */
  void init(UpdateCallback callback);

  /**
   * @brief Start the background update checking thread.
   */
  void start();

  /**
   * @brief Stop the background update checking thread.
   */
  void stop();

  /**
   * @brief Perform a single update check.
   * @return UpdateInfo containing the results
   */
  UpdateInfo check_for_updates();

  /**
   * @brief Parse version string into components.
   * @param version Version string to parse
   * @return Vector of version components
   */
  std::vector<int> parse_version(const std::string &version);

}  // namespace update_checker
