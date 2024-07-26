#pragma once

// local includes
#include "display_device.h"

namespace display_device {

  /**
   * @brief Stringify a device_state_e value.
   * @param value Value to be stringified.
   * @return A string representation of device_state_e value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(device_state_e { });
   * ```
   */
  std::string
  to_string(device_state_e value);

  /**
   * @brief Stringify a hdr_state_e value.
   * @param value Value to be stringified.
   * @return A string representation of hdr_state_e value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(hdr_state_e { });
   * ```
   */
  std::string
  to_string(hdr_state_e value);

  /**
   * @brief Stringify a hdr_state_map_t value.
   * @param value Value to be stringified.
   * @return A string representation of hdr_state_map_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(hdr_state_map_t { });
   * ```
   */
  std::string
  to_string(const hdr_state_map_t &value);

  /**
   * @brief Stringify a device_info_t value.
   * @param value Value to be stringified.
   * @return A string representation of device_info_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(device_info_t { });
   * ```
   */
  std::string
  to_string(const device_info_t &value);

  /**
   * @brief Stringify a device_info_map_t value.
   * @param value Value to be stringified.
   * @return A string representation of device_info_map_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(device_info_map_t { });
   * ```
   */
  std::string
  to_string(const device_info_map_t &value);

  /**
   * @brief Stringify a resolution_t value.
   * @param value Value to be stringified.
   * @return A string representation of resolution_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(resolution_t { });
   * ```
   */
  std::string
  to_string(const resolution_t &value);

  /**
   * @brief Stringify a refresh_rate_t value.
   * @param value Value to be stringified.
   * @return A string representation of refresh_rate_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(refresh_rate_t { });
   * ```
   */
  std::string
  to_string(const refresh_rate_t &value);

  /**
   * @brief Stringify a display_mode_t value.
   * @param value Value to be stringified.
   * @return A string representation of display_mode_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(display_mode_t { });
   * ```
   */
  std::string
  to_string(const display_mode_t &value);

  /**
   * @brief Stringify a device_display_mode_map_t value.
   * @param value Value to be stringified.
   * @return A string representation of device_display_mode_map_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(device_display_mode_map_t { });
   * ```
   */
  std::string
  to_string(const device_display_mode_map_t &value);

  /**
   * @brief Stringify a active_topology_t value.
   * @param value Value to be stringified.
   * @return A string representation of active_topology_t value.
   *
   * EXAMPLES:
   * ```cpp
   * const std::string string_value = to_string(active_topology_t { });
   * ```
   */
  std::string
  to_string(const active_topology_t &value);

}  // namespace display_device
