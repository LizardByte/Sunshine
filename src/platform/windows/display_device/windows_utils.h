#pragma once

// the most stupid windows include (because it needs to be first...)
#include <windows.h>

// local includes
#include "src/display_device/display_device.h"

namespace display_device::w_utils {

  constexpr bool ACTIVE_ONLY_DEVICES { true }; /**< The device path must be active. */
  constexpr bool ALL_DEVICES { false }; /**< The device path can be active or inactive. */

  /**
   * @brief Contains currently available paths and associated modes.
   */
  struct path_and_mode_data_t {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths; /**< Available display paths. */
    std::vector<DISPLAYCONFIG_MODE_INFO> modes; /**< Display modes for ACTIVE displays. */
  };

  /**
   * @brief Contains the device path and the id for a VALID device.
   * @see get_device_info_for_valid_path for what is considered a valid device.
   * @see get_device_id for how we make the device id.
   */
  struct device_info_t {
    std::string device_path; /**< Unique device path string. */
    std::string device_id; /**< A device id (made up by us) that is identifies the device. */
  };

  /**
   * @brief Stringify the error code from Windows API.
   * @param error_code Error code to stringify.
   * @returns String containing the error code in a readable format + a system message describing the code.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string error_message = get_error_string(ERROR_NOT_SUPPORTED);
   * ```
   */
  std::string
  get_error_string(LONG error_code);

  /**
   * @brief Check if the display's source mode is primary - if the associated device is a primary display device.
   * @param mode Mode to check.
   * @returns True if the mode's origin point is at (0, 0) coordinate (primary), false otherwise.
   * @note It is possible to have multiple primary source modes at the same time.
   * @see get_source_mode on how to get the source mode.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_SOURCE_MODE mode;
   * const bool is_primary = is_primary(mode);
   * ```
   */
  bool
  is_primary(const DISPLAYCONFIG_SOURCE_MODE &mode);

  /**
   * @brief Check if the source modes are duplicated (cloned).
   * @param mode_a First mode to check.
   * @param mode_b Second mode to check.
   * @returns True if both mode have the same origin point, false otherwise.
   * @note Windows enforces the behaviour that only the duplicate devices can
   *       have the same origin point as otherwise the configuration is considered invalid by the OS.
   * @see get_source_mode on how to get the source mode.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_SOURCE_MODE mode_a;
   * DISPLAYCONFIG_SOURCE_MODE mode_b;
   * const bool are_duplicated = are_modes_duplicated(mode_a, mode_b);
   * ```
   */
  bool
  are_modes_duplicated(const DISPLAYCONFIG_SOURCE_MODE &mode_a, const DISPLAYCONFIG_SOURCE_MODE &mode_b);

  /**
   * @brief Check if the display device path's target is available.
   *
   * In most cases this this would mean physically connected to the system,
   * but it also possible force the path to persist. It is not clear if it be
   * counted as available or not.
   *
   * @param path Path to check.
   * @returns True if path's target is marked as available, false otherwise.
   * @see query_display_config on how to get paths from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const bool available = is_available(path);
   * ```
   */
  bool
  is_available(const DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Check if the display device path is marked as active.
   * @param path Path to check.
   * @returns True if path is marked as active, false otherwise.
   * @see query_display_config on how to get paths from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const bool active = is_active(path);
   * ```
   */
  bool
  is_active(const DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Mark the display device path as active.
   * @param path Path to mark.
   * @see query_display_config on how to get paths from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * if (!is_active(path)) {
   *   set_active(path);
   * }
   * ```
   */
  void
  set_active(DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Get a stable and persistent device id for the path.
   *
   * This function tries to generate a unique id for the path that
   * is persistent between driver re-installs and physical unplugging and
   * replugging of the device.
   *
   * The best candidate for it could have been a "ContainerID" from the
   * registry, however it was found to be unstable for the virtual display
   * (probably because it uses the EDID for the id generation and the current
   * virtual displays have incomplete EDID information). The "ContainerID"
   * also does not change if the physical device is plugged into a different
   * port and seems to be very stable, however because of virtual displays
   * other solution was used.
   *
   * The accepted solution was to use the "InstanceID" and EDID (just to be
   * on the safe side). "InstanceID" is semi-stable, it has some parts that
   * change between driver re-installs and it has a part that changes based
   * on the GPU port that the display is connected to. It is most likely to
   * be unique, but since the MS documentation is lacking we are also hashing
   * EDID information (contains serial ids, timestamps and etc. that should
   * guarantee that identical displays are differentiated like with the
   * "ContainerID"). Most importantly this information is stable for the virtual
   * displays.
   *
   * After we remove the unstable parts from the "InstanceID" and hash everything
   * together, we get an id that changes only when you connect the display to
   * a different GPU port which seems to be acceptable.
   *
   * As a fallback we are using a hashed device path, in case the "InstanceID" or
   * EDID is not available. At least if you don't do driver re-installs often
   * and change the GPU ports, it will be stable for a while.
   *
   * @param path Path to get the device id for.
   * @returns Device id, or an empty string if it could not be generated.
   * @see query_display_config on how to get paths from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const std::string device_path = get_device_id(path);
   * ```
   */
  std::string
  get_device_id(const DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Get a string that represents a path from the adapter to the display target.
   * @param path Path to get the string for.
   * @returns String representation, or an empty string if it's not available.
   * @see query_display_config on how to get paths from the system.
   * @note In the rest of the code we refer to this string representation simply as the "device path".
   *       It is used as a simple way of grouping related path objects together and removing "bad" paths
   *       that don't have such string representation.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const std::string device_path = get_monitor_device_path(path);
   * ```
   */
  std::string
  get_monitor_device_path(const DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Get the user friendly name for the path.
   * @param path Path to get user friendly name for.
   * @returns User friendly name for the path if available, empty string otherwise.
   * @see query_display_config on how to get paths from the system.
   * @note This is usually a monitor name (like "ROG PG279Q") and is most likely take from EDID.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const std::string friendly_name = get_friendly_name(path);
   * ```
   */
  std::string
  get_friendly_name(const DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Get the logical display name for the path.
   *
   * These are the "\\\\.\\DISPLAY1", "\\\\.\\DISPLAY2" and etc. display names that can
   * change whenever Windows wants to change them.
   *
   * @param path Path to get user display name for.
   * @returns Display name for the path if available, empty string otherwise.
   * @see query_display_config on how to get paths from the system.
   * @note Inactive paths can have these names already assigned to them, even
   *       though they are not even in use! There can also be duplicates.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const std::string display_name = get_display_name(path);
   * ```
   */
  std::string
  get_display_name(const DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Get the HDR state the path.
   * @param path Path to get HDR state for.
   * @returns hdr_state_e::unknown if the state could not be retrieved, or other enum values describing the state otherwise.
   * @see query_display_config on how to get paths from the system.
   * @see hdr_state_e
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const auto hdr_state = get_hdr_state(path);
   * ```
   */
  hdr_state_e
  get_hdr_state(const DISPLAYCONFIG_PATH_INFO &path);

  /**
   * @brief Set the HDR state for the path.
   * @param path Path to set HDR state for.
   * @param enable Specify whether to enable or disable HDR state.
   * @returns True if new HDR state was set, false otherwise.
   * @see query_display_config on how to get paths from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const bool success = set_hdr_state(path, false);
   * ```
   */
  bool
  set_hdr_state(const DISPLAYCONFIG_PATH_INFO &path, bool enable);

  /**
   * @brief Get the source mode index from the path.
   *
   * It performs sanity checks on the modes list that the index is indeed correct.
   *
   * @param path Path to get the source mode index for.
   * @param modes A list of various modes (source, target, desktop and probably more in the future).
   * @returns Valid index value if it's found in the modes list and the mode at that index is of a type "source" mode,
   *          empty optional otherwise.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * std::vector<DISPLAYCONFIG_MODE_INFO> modes;
   * const auto source_index = get_source_index(path, modes);
   * ```
   */
  boost::optional<UINT32>
  get_source_index(const DISPLAYCONFIG_PATH_INFO &path, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

  /**
   * @brief Set the source mode index in the path.
   * @param path Path to modify.
   * @param index Index value to set or empty optional to mark the index as invalid.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * set_source_index(path, 5);
   * set_source_index(path, boost::none);
   * ```
   */
  void
  set_source_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index);

  /**
   * @brief Set the target mode index in the path.
   * @param path Path to modify.
   * @param index Index value to set or empty optional to mark the index as invalid.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * set_target_index(path, 5);
   * set_target_index(path, boost::none);
   * ```
   */
  void
  set_target_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index);

  /**
   * @brief Set the desktop mode index in the path.
   * @param path Path to modify.
   * @param index Index value to set or empty optional to mark the index as invalid.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * set_desktop_index(path, 5);
   * set_desktop_index(path, boost::none);
   * ```
   */
  void
  set_desktop_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index);

  /**
   * @brief Set the clone group id in the path.
   * @param path Path to modify.
   * @param id Id value to set or empty optional to mark the id as invalid.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * set_clone_group_id(path, 5);
   * set_clone_group_id(path, boost::none);
   * ```
   */
  void
  set_clone_group_id(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &id);

  /**
   * @brief Get the source mode from the list at the specified index.
   *
   * This function does additional sanity checks for the modes list and ensures
   * that the mode at the specified index is indeed a source mode.
   *
   * @param index Index to get the mode for. It is of boost::optional type
   *              as the function is intended to be used with get_source_index function.
   * @param modes List to get the mode from.
   * @returns A pointer to a valid source mode from to list at the specified index, nullptr otherwise.
   * @see query_display_config on how to get paths and modes from the system.
   * @see get_source_index
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const std::vector<DISPLAYCONFIG_MODE_INFO> modes;
   * const DISPLAYCONFIG_SOURCE_MODE* source_mode = get_source_mode(get_source_index(path, modes), modes);
   * ```
   */
  const DISPLAYCONFIG_SOURCE_MODE *
  get_source_mode(const boost::optional<UINT32> &index, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

  /**
   * @brief Get the source mode from the list at the specified index.
   *
   * This function does additional sanity checks for the modes list and ensures
   * that the mode at the specified index is indeed a source mode.
   *
   * @param index Index to get the mode for. It is of boost::optional type
   *              as the function is intended to be used with get_source_index function.
   * @param modes List to get the mode from.
   * @returns A pointer to a valid source mode from to list at the specified index, nullptr otherwise.
   * @see query_display_config on how to get paths and modes from the system.
   * @see get_source_index
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * std::vector<DISPLAYCONFIG_MODE_INFO> modes;
   * DISPLAYCONFIG_SOURCE_MODE* source_mode = get_source_mode(get_source_index(path, modes), modes);
   * ```
   */
  DISPLAYCONFIG_SOURCE_MODE *
  get_source_mode(const boost::optional<UINT32> &index, std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

  /**
   * @brief Validate the path and get the commonly used information from it.
   *
   * This a convenience function to ensure that our concept of "valid path" remains the
   * same throughout the code.
   *
   * Currently, for use, a valid path is:
   *   - a path with and available display target;
   *   - a path that is active (optional);
   *   - a path that has a non-empty device path;
   *   - a path that has a non-empty device id;
   *   - a path that has a non-empty device name assigned.
   *
   * @param path Path to validate and get info for.
   * @param must_be_active Optionally request that the valid path must also be active.
   * @returns Commonly used info for the path, or empty optional if the path is invalid.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * DISPLAYCONFIG_PATH_INFO path;
   * const auto device_info = get_device_info_for_valid_path(path, true);
   * ```
   */
  boost::optional<device_info_t>
  get_device_info_for_valid_path(const DISPLAYCONFIG_PATH_INFO &path, bool must_be_active);

  /**
   * @brief Query Windows for the device paths and associated modes.
   * @param active_only Specify to query for active devices only.
   * @returns Data containing paths and modes, empty optional if we have failed to query.
   *
   * EXAMPLES:
   * ```cpp
   * const auto display_data = query_display_config(true);
   * ```
   */
  boost::optional<path_and_mode_data_t>
  query_display_config(bool active_only);

  /**
   * @brief Get the active path matching the device id.
   * @param device_id Id to search for in the the list.
   * @param paths List to be searched.
   * @returns A pointer to an active path matching our id, nullptr otherwise.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * const std::vector<DISPLAYCONFIG_PATH_INFO> paths;
   * const DISPLAYCONFIG_PATH_INFO* active_path = get_active_path("MY_DEVICE_ID", paths);
   * ```
   */
  const DISPLAYCONFIG_PATH_INFO *
  get_active_path(const std::string &device_id, const std::vector<DISPLAYCONFIG_PATH_INFO> &paths);

  /**
   * @brief Get the active path matching the device id.
   * @param device_id Id to search for in the the list.
   * @param paths List to be searched.
   * @returns A pointer to an active path matching our id, nullptr otherwise.
   * @see query_display_config on how to get paths and modes from the system.
   *
   * EXAMPLES:
   * ```cpp
   * std::vector<DISPLAYCONFIG_PATH_INFO> paths;
   * DISPLAYCONFIG_PATH_INFO* active_path = get_active_path("MY_DEVICE_ID", paths);
   * ```
   */
  DISPLAYCONFIG_PATH_INFO *
  get_active_path(const std::string &device_id, std::vector<DISPLAYCONFIG_PATH_INFO> &paths);

  /**
   * @brief Check whether the user session is locked.
   * @returns True if it's definitely known that the session is locked, false otherwise.
   *
   * EXAMPLES:
   * ```cpp
   * const bool is_locked { is_user_session_locked() };
   * ```
   */
  bool
  is_user_session_locked();

  /**
   * @brief Check whether it is already known that the CCD API will fail to set settings.
   * @returns True if we already known we don't have access (for now), false otherwise.
   *
   * EXAMPLES:
   * ```cpp
   * const bool no_access { test_no_access_to_ccd_api() };
   * ```
   */
  bool
  test_no_access_to_ccd_api();

}  // namespace display_device::w_utils
