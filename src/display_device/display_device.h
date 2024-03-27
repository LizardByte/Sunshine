#pragma once

// standard includes
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

// lib includes
#include <boost/optional.hpp>
#include <nlohmann/json.hpp>

namespace display_device {

  /**
   * @brief The device state in the operating system.
   * @note On Windows you can have have multiple primary displays when they are duplicated.
   */
  enum class device_state_e {
    inactive,
    active,
    primary /**< Primary state is also implicitly active. */
  };

  /**
   * @brief The device's HDR state in the operating system.
   */
  enum class hdr_state_e {
    unknown, /**< HDR state could not be retrieved from the OS (even if the display supports it). */
    disabled,
    enabled
  };

  // For JSON serialization for hdr_state_e
  NLOHMANN_JSON_SERIALIZE_ENUM(hdr_state_e, { { hdr_state_e::unknown, "unknown" },
                                              { hdr_state_e::disabled, "disabled" },
                                              { hdr_state_e::enabled, "enabled" } })

  /**
   * @brief Ordered map of [DEVICE_ID -> hdr_state_e].
   */
  using hdr_state_map_t = std::map<std::string, hdr_state_e>;

  /**
   * @brief The device's HDR state in the operating system.
   */
  struct device_info_t {
    std::string display_name; /**< A name representing the OS display (source) the device is connected to. */
    std::string friendly_name; /**< A human-readable name for the device. */
    device_state_e device_state; /**< Device's state. @see device_state_e */
    hdr_state_e hdr_state; /**< Device's HDR state. @see hdr_state_e */
  };

  /**
   * @brief Ordered map of [DEVICE_ID -> device_info_t].
   * @see device_info_t
   */
  using device_info_map_t = std::map<std::string, device_info_t>;

  /**
   * @brief Display's resolution.
   */
  struct resolution_t {
    unsigned int width;
    unsigned int height;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(resolution_t, width, height)
  };

  /**
   * @brief Display's refresh rate.
   * @note Floating point is stored in a "numerator/denominator" form.
   */
  struct refresh_rate_t {
    unsigned int numerator;
    unsigned int denominator;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(refresh_rate_t, numerator, denominator)
  };

  /**
   * @brief Display's mode (resolution + refresh rate).
   * @see resolution_t
   * @see refresh_rate_t
   */
  struct display_mode_t {
    resolution_t resolution;
    refresh_rate_t refresh_rate;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(display_mode_t, resolution, refresh_rate)
  };

  /**
   * @brief Ordered map of [DEVICE_ID -> display_mode_t].
   * @see display_mode_t
   */
  using device_display_mode_map_t = std::map<std::string, display_mode_t>;

  /**
   * @brief A LIST[LIST[DEVICE_ID]] structure which represents an active topology.
   *
   * Single display:
   *     [[DISPLAY_1]]
   * 2 extended displays:
   *     [[DISPLAY_1], [DISPLAY_2]]
   * 2 duplicated displays:
   *     [[DISPLAY_1, DISPLAY_2]]
   * Mixed displays:
   *     [[EXTENDED_DISPLAY_1], [DUPLICATED_DISPLAY_1, DUPLICATED_DISPLAY_2], [EXTENDED_DISPLAY_2]]
   *
   * @note On Windows the order does not matter of both device ids or the inner lists.
   */
  using active_topology_t = std::vector<std::vector<std::string>>;

  /**
   * @brief Enumerate the available (active and inactive) devices.
   * @returns A map of available devices.
   *          Empty map can also be returned if an error has occurred.
   *
   * EXAMPLES:
   * ```cpp
   * const auto devices { enum_available_devices() };
   * ```
   */
  device_info_map_t
  enum_available_devices();

  /**
   * @brief Get display name associated with the device.
   * @param device_id A device to get display name for.
   * @returns A display name for the device, or an empty string if the device is inactive or not found.
   *          Empty string can also be returned if an error has occurred.
   * @see device_info_t
   *
   * EXAMPLES:
   * ```cpp
   * const std::string device_name { "MY_DEVICE_ID" };
   * const std::string display_name = get_display_name(device_id);
   * ```
   */
  std::string
  get_display_name(const std::string &device_id);

  /**
   * @brief Get current display modes for the devices.
   * @param device_ids A list of devices to get the modes for.
   * @returns A map of device modes per a device or an empty map if a mode could not be found (e.g. device is inactive).
   *          Empty map can also be returned if an error has occurred.
   *
   * EXAMPLES:
   * ```cpp
   * const std::unordered_set<std::string> device_ids { "DEVICE_ID_1", "DEVICE_ID_2" };
   * const auto current_modes = get_current_display_modes(device_ids);
   * ```
   */
  device_display_mode_map_t
  get_current_display_modes(const std::unordered_set<std::string> &device_ids);

  /**
   * @brief Set new display modes for the devices.
   * @param modes A map of modes to set.
   * @returns True if modes were set, false otherwise.
   * @warning if any of the specified devices are duplicated, modes modes be provided
   *          for duplicates too!
   *
   * EXAMPLES:
   * ```cpp
   * const std::string display_a { "MY_ID_1" };
   * const std::string display_b { "MY_ID_2" };
   * const auto success = set_display_modes({ { display_a, { { 1920, 1080 }, { 60, 1 } } },
   *                                          { display_b, { { 1920, 1080 }, { 120, 1 } } } });
   * ```
   */
  bool
  set_display_modes(const device_display_mode_map_t &modes);

  /**
   * @brief Check whether the specified device is primary.
   * @param device_id A device to perform the check for.
   * @returns True if the device is primary, false otherwise.
   * @see device_state_e
   *
   * EXAMPLES:
   * ```cpp
   * const std::string device_id { "MY_DEVICE_ID" };
   * const bool is_primary = is_primary_device(device_id);
   * ```
   */
  bool
  is_primary_device(const std::string &device_id);

  /**
   * @brief Set the device as a primary display.
   * @param device_id A device to set as primary.
   * @returns True if the device is or was set as primary, false otherwise.
   * @note On Windows if the device is duplicated, the other duplicated device(-s) will also become a primary device.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string device_id { "MY_DEVICE_ID" };
   * const bool success = set_as_primary_device(device_id);
   * ``
   */
  bool
  set_as_primary_device(const std::string &device_id);

  /**
   * @brief Get HDR state for the devices.
   * @param device_ids A list of devices to get the HDR states for.
   * @returns A map of HDR states per a device or an empty map if an error has occurred.
   * @note On Windows the state cannot be retrieved until the device is active even if it supports it.
   *
   * EXAMPLES:
   * ```cpp
   * const std::unordered_set<std::string> device_ids { "DEVICE_ID_1", "DEVICE_ID_2" };
   * const auto current_hdr_states = get_current_hdr_states(device_ids);
   * ```
   */
  hdr_state_map_t
  get_current_hdr_states(const std::unordered_set<std::string> &device_ids);

  /**
   * @brief Set HDR states for the devices.
   * @param modes A map of HDR states to set.
   * @returns True if HDR states were set, false otherwise.
   * @note If `unknown` states are provided, they will be silently ignored
   *       and current state will not be changed.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string display_a { "MY_ID_1" };
   * const std::string display_b { "MY_ID_2" };
   * const auto success = set_hdr_states({ { display_a, hdr_state_e::enabled },
   *                                       { display_b, hdr_state_e::disabled } });
   * ```
   */
  bool
  set_hdr_states(const hdr_state_map_t &states);

  /**
   * @brief Get the active (current) topology.
   * @returns A list representing the current topology.
   *          Empty list can also be returned if an error has occurred.
   *
   * EXAMPLES:
   * ```cpp
   * const auto current_topology { get_current_topology() };
   * ```
   */
  active_topology_t
  get_current_topology();

  /**
   * @brief Verify if the active topology is valid.
   *
   * This is mostly meant as a sanity check or to verify that it is still valid
   * after a manual modification to an existing topology.
   *
   * @param topology Topology to validated.
   * @returns True if it is valid, false otherwise.
   *
   * EXAMPLES:
   * ```cpp
   * auto current_topology { get_current_topology() };
   * // Modify the current_topology
   * const bool is_valid = is_topology_valid(current_topology);
   * ```
   */
  bool
  is_topology_valid(const active_topology_t &topology);

  /**
   * @brief Check if the topologies are close enough to be considered the same by the OS.
   * @param topology_a First topology to compare.
   * @param topology_b Second topology to compare.
   * @returns True if topologies are close enough, false otherwise.
   *
   * EXAMPLES:
   * ```cpp
   * auto current_topology { get_current_topology() };
   * auto new_topology { current_topology };
   * // Modify the new_topology
   * const bool is_the_same = is_topology_the_same(current_topology, new_topology);
   * ```
   */
  bool
  is_topology_the_same(const active_topology_t &topology_a, const active_topology_t &topology_b);

  /**
   * @brief Set the a new active topology for the OS.
   * @param new_topology New device topology to set.
   * @returns True if the new topology has been set, false otherwise.
   *
   * EXAMPLES:
   * ```cpp
   * auto current_topology { get_current_topology() };
   * // Modify the current_topology
   * const bool success = set_topology(current_topology);
   * ```
   */
  bool
  set_topology(const active_topology_t &new_topology);

}  // namespace display_device
