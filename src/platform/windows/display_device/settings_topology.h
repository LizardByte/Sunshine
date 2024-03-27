#pragma once

// local includes
#include "src/display_device/settings.h"

namespace display_device {

  /**
   * @brief Contains metadata about the current topology.
   */
  struct topology_metadata_t {
    active_topology_t current_topology; /**< The currently active topology. */
    std::unordered_set<std::string> newly_enabled_devices; /**< A list of device ids that were newly enabled after changing topology. */
    bool primary_device_requested; /**< Indicates that the user did NOT specify device id to be used. */
    std::vector<std::string> duplicated_devices; /**< A list of devices id that we need to handle. If user specified device id, it will always be the first entry. */
  };

  /**
   * @brief Container for active topologies.
   * @note Both topologies can be the same.
   */
  struct topology_pair_t {
    active_topology_t initial; /**< The initial topology that we had before we switched. */
    active_topology_t modified; /**< The topology that we have modified. */

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(topology_pair_t, initial, modified)
  };

  /**
   * @brief Contains the result after handling the configuration.
   * @see handle_device_topology_configuration
   */
  struct handled_topology_result_t {
    topology_pair_t pair;
    topology_metadata_t metadata;
  };

  /**
   * @brief Get all ids from the active topology structure.
   * @param topology Topology to get ids from.
   * @returns A list of device ids.
   *
   * EXAMPLES:
   * ```cpp
   * const auto device_ids = get_device_ids_from_topology(get_current_topology());
   * ```
   */
  std::unordered_set<std::string>
  get_device_ids_from_topology(const active_topology_t &topology);

  /**
   * @brief Get new device ids that were not present in previous topology.
   * @param previous_topology The previous topology.
   * @param new_topology A new topology.
   * @return A list of devices ids.
   *
   * EXAMPLES:
   * ```cpp
   * active_topology_t old_topology { { "ID_1" } };
   * active_topology_t new_topology { { "ID_1" }, { "ID_2" } };
   * const auto device_ids = get_newly_enabled_devices_from_topology(old_topology, new_topology);
   * // device_ids contains "ID_2"
   * ```
   */
  std::unordered_set<std::string>
  get_newly_enabled_devices_from_topology(const active_topology_t &previous_topology, const active_topology_t &new_topology);

  /**
   * @brief Modify the topology based on the configuration and previously configured topology.
   *
   * The function performs the necessary steps for changing topology if needed.
   * It evaluates the previous configuration in case we are just updating
   * some of the settings (like resolution) where topology change might not be necessary.
   *
   * In case the function determines that we need to revert all of the previous settings
   * since the new topology is not compatible with the previously configured one, the revert_settings
   * parameter will be called to completely revert all changes.
   *
   * @param config Configuration to be evaluated.
   * @param previously_configured_topology A result from a earlier call of this function.
   * @param revert_settings A function-proxy that can be used to revert all of the changes made to the device displays.
   * @return A result object, or an empty optional if the function fails.
   */
  boost::optional<handled_topology_result_t>
  handle_device_topology_configuration(const parsed_config_t &config, const boost::optional<topology_pair_t> &previously_configured_topology, const std::function<bool()> &revert_settings);

}  // namespace display_device
