/**
 * @file src/platform/permissions.h
 * @brief Cross-platform permission status and request interface.
 */
#pragma once

// standard includes
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace platf {
  /**
   * @brief Access needed by the current Sunshine configuration.
   */
  struct permission_status_t {
    std::string id;  ///< Stable identifier used by the Web UI.
    std::string status;  ///< Granted, denied, not_determined, on_use, or unknown.
    bool required;  ///< Whether the current configuration needs this access.
    bool verifiable;  ///< Whether the OS permits a passive status check.
    bool requestable = false;  ///< Whether Sunshine can initiate a native request or settings action.
  };

  /**
   * @brief Query access needed by Sunshine on the current platform.
   *
   * @return Permission status records for the Web UI.
   */
  std::vector<permission_status_t> get_permission_statuses();

  /**
   * @brief Determine whether all verifiable required permissions are granted.
   *
   * @param permissions Current permission status records.
   * @return True when no verifiable required permission is missing.
   */
  bool required_permissions_granted(const std::vector<permission_status_t> &permissions);

  /**
   * @brief Initiate a native request or settings action for an access item.
   *
   * On macOS, Local Network opens Privacy & Security, where the user selects Local Network.
   *
   * @param id Stable identifier of the access item.
   * @return True when the action was recognized and initiated.
   */
  bool request_permission(std::string_view id);

  /**
   * @brief Check whether this process can list and create files in a directory.
   *
   * @param path Directory to check without modifying its contents.
   * @return True when the directory is present and accessible.
   */
  bool can_access_directory(const std::filesystem::path &path);
}  // namespace platf
