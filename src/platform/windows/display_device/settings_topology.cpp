// local includes
#include "settings_topology.h"
#include "src/display_device/to_string.h"
#include "src/logging.h"

namespace display_device {

  namespace {

    /**
     * @brief Enumerate and get one of the devices matching the id or
     *        any of the primary devices if id is unspecified.
     * @param device_id Id to find in enumerated devices.
     * @return Device id, or empty string if an error has occurred.
     *
     * EXAMPLES:
     * ```cpp
     * const std::string primary_device = find_one_of_the_available_devices("");
     * const std::string id_that_matches_provided_id = find_one_of_the_available_devices(primary_device);
     * ```
     */
    std::string
    find_one_of_the_available_devices(const std::string &device_id) {
      const auto devices { enum_available_devices() };
      if (devices.empty()) {
        BOOST_LOG(error) << "Display device list is empty!";
        return {};
      }
      BOOST_LOG(info) << "Available display devices: " << to_string(devices);

      const auto device_it { std::find_if(std::begin(devices), std::end(devices), [&device_id](const auto &entry) {
        return device_id.empty() ? entry.second.device_state == device_state_e::primary : entry.first == device_id;
      }) };
      if (device_it == std::end(devices)) {
        BOOST_LOG(error) << "Device " << (device_id.empty() ? "PRIMARY" : device_id) << " not found in the list of available devices!";
        return {};
      }

      return device_it->first;
    }

    /**
     * @brief Get all device ids that belong in the same group as provided ids (duplicated displays).
     * @param device_id Device id to search for in the topology.
     * @param topology Topology to search.
     * @return A list of device ids, with the provided device id always at the front.
     *
     * EXAMPLES:
     * ```cpp
     * const auto duplicated_devices = get_duplicate_devices("MY_DEVICE_ID", get_current_topology());
     * ```
     */
    std::vector<std::string>
    get_duplicate_devices(const std::string &device_id, const active_topology_t &topology) {
      std::vector<std::string> duplicated_devices;

      duplicated_devices.clear();
      duplicated_devices.push_back(device_id);

      for (const auto &group : topology) {
        for (const auto &group_device_id : group) {
          if (device_id == group_device_id) {
            std::copy_if(std::begin(group), std::end(group), std::back_inserter(duplicated_devices), [&](const auto &id) {
              return id != device_id;
            });
            break;
          }
        }
      }

      return duplicated_devices;
    }

    /**
     * @brief Check if device id is found in the active topology.
     * @param device_id Device id to search for in the topology.
     * @param topology Topology to search.
     * @return True if device id is in the topology, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * const bool is_in_topology = is_device_found_in_active_topology("MY_DEVICE_ID", get_current_topology());
     * ```
     */
    bool
    is_device_found_in_active_topology(const std::string &device_id, const active_topology_t &topology) {
      for (const auto &group : topology) {
        for (const auto &group_device_id : group) {
          if (device_id == group_device_id) {
            return true;
          }
        }
      }

      return false;
    }

    /**
     * @brief Compute the final topology based on the information we have.
     * @param device_prep The device preparation setting from user configuration.
     * @param primary_device_requested  Indicates that the user did NOT specify device id to be used.
     * @param duplicated_devices Devices that we need to handle.
     * @param topology The current topology that we are evaluating.
     * @return Topology that matches requirements and should be set.
     */
    active_topology_t
    determine_final_topology(parsed_config_t::device_prep_e device_prep, const bool primary_device_requested, const std::vector<std::string> &duplicated_devices, const active_topology_t &topology) {
      boost::optional<active_topology_t> final_topology;

      const bool topology_change_requested { device_prep != parsed_config_t::device_prep_e::no_operation };
      if (topology_change_requested) {
        if (device_prep == parsed_config_t::device_prep_e::ensure_only_display) {
          // Device needs to be the only one that's active or if it's a PRIMARY device,
          // only the whole PRIMARY group needs to be active (in case they are duplicated)

          if (primary_device_requested) {
            if (topology.size() > 1) {
              // There are other topology groups other than the primary devices,
              // so we need to change that
              final_topology = active_topology_t { { duplicated_devices } };
            }
            else {
              // Primary device group is the only one active, nothing to do
            }
          }
          else {
            // Since primary_device_requested == false, it means a device was specified via config by the user
            // and is the only device that needs to be enabled

            if (is_device_found_in_active_topology(duplicated_devices.front(), topology)) {
              // Device is currently active in the active topology group

              if (duplicated_devices.size() > 1 || topology.size() > 1) {
                // We have more than 1 device in the group, or we have more than 1 topology groups.
                // We need to disable all other devices
                final_topology = active_topology_t { { duplicated_devices.front() } };
              }
              else {
                // Our device is the only one that's active, nothing to do
              }
            }
            else {
              // Our device is not active, we need to activate it and ONLY it
              final_topology = active_topology_t { { duplicated_devices.front() } };
            }
          }
        }
        // device_prep_e::ensure_active || device_prep_e::ensure_primary
        else {
          //  The device needs to be active at least.

          if (primary_device_requested || is_device_found_in_active_topology(duplicated_devices.front(), topology)) {
            // Device is already active, nothing to do here
          }
          else {
            // Create the extended topology as it's probably what makes sense the most...
            final_topology = topology;
            final_topology->push_back({ duplicated_devices.front() });
          }
        }
      }

      return final_topology ? *final_topology : topology;
    }

  }  // namespace

  std::unordered_set<std::string>
  get_device_ids_from_topology(const active_topology_t &topology) {
    std::unordered_set<std::string> device_ids;
    for (const auto &group : topology) {
      for (const auto &device_id : group) {
        device_ids.insert(device_id);
      }
    }

    return device_ids;
  }

  std::unordered_set<std::string>
  get_newly_enabled_devices_from_topology(const active_topology_t &previous_topology, const active_topology_t &new_topology) {
    const auto prev_ids { get_device_ids_from_topology(previous_topology) };
    auto new_ids { get_device_ids_from_topology(new_topology) };

    for (auto &id : prev_ids) {
      new_ids.erase(id);
    }

    return new_ids;
  }

  boost::optional<handled_topology_result_t>
  handle_device_topology_configuration(const parsed_config_t &config, const boost::optional<topology_pair_t> &previously_configured_topology, const std::function<bool()> &revert_settings) {
    const bool primary_device_requested { config.device_id.empty() };
    const std::string requested_device_id { find_one_of_the_available_devices(config.device_id) };
    if (requested_device_id.empty()) {
      // Error already logged
      return boost::none;
    }

    // If we still have a previously configured topology, we could potentially skip making any changes to the topology.
    // However, it could also mean that we need to revert any previous changes in case the final topology has changed somehow.
    if (previously_configured_topology) {
      // Here we are pretending to be in an initial topology and want to perform reevaluation in case the
      // user has changed the settings while the stream was paused. For the proper "evaluation" order,
      // see logic outside this conditional.
      const auto prev_duplicated_devices { get_duplicate_devices(requested_device_id, previously_configured_topology->initial) };
      const auto prev_final_topology { determine_final_topology(config.device_prep, primary_device_requested, prev_duplicated_devices, previously_configured_topology->initial) };

      // There is also an edge case where we can have a different number of primary duplicated devices, which wasn't the case
      // during the initial topology configuration. If the user requested to use the primary device,
      // the prev_final_topology would not reflect that change in primary duplicated devices. Therefore, we also need
      // to evaluate current topology (which would have the new state of primary devices) and arrive at the
      // same final topology as the prev_final_topology.
      const auto current_topology { get_current_topology() };
      const auto duplicated_devices { get_duplicate_devices(requested_device_id, current_topology) };
      const auto final_topology { determine_final_topology(config.device_prep, primary_device_requested, duplicated_devices, current_topology) };

      // If the topology we are switching to is the same as the final topology we had before, that means
      // user did not change anything, and we don't need to revert changes.
      if (!is_topology_the_same(previously_configured_topology->modified, prev_final_topology) ||
          !is_topology_the_same(previously_configured_topology->modified, final_topology)) {
        BOOST_LOG(warning) << "Previous topology does not match the new one. Reverting previous changes!";
        if (!revert_settings()) {
          return boost::none;
        }
      }
    }

    // Regardless of whether the user has made any changes to the user configuration or not, we always
    // need to evaluate the current topology and perform the switch if needed as the user might
    // have been playing around with active displays while the stream was paused.

    const auto current_topology { get_current_topology() };
    if (!is_topology_valid(current_topology)) {
      BOOST_LOG(error) << "Display topology is invalid!";
      return boost::none;
    }

    // When dealing with the "requested device" here and in other functions we need to keep
    // in mind that it could belong to a duplicated display and thus all of them
    // need to be taken into account, which complicates everything...
    auto duplicated_devices { get_duplicate_devices(requested_device_id, current_topology) };
    const auto final_topology { determine_final_topology(config.device_prep, primary_device_requested, duplicated_devices, current_topology) };

    BOOST_LOG(debug) << "Current display topology: " << to_string(current_topology);
    if (!is_topology_the_same(current_topology, final_topology)) {
      BOOST_LOG(info) << "Changing display topology to: " << to_string(final_topology);
      if (!set_topology(final_topology)) {
        // Error already logged.
        return boost::none;
      }

      // It is possible that we no longer have duplicate displays, so we need to update the list
      duplicated_devices = get_duplicate_devices(requested_device_id, final_topology);
    }

    // This check is mainly to cover the case for "config.device_prep == no_operation" as we at least
    // have to validate that the device exists, but it doesn't hurt to double-check it in all cases.
    if (!is_device_found_in_active_topology(requested_device_id, final_topology)) {
      BOOST_LOG(error) << "Device " << requested_device_id << " is not active!";
      return boost::none;
    }

    return handled_topology_result_t {
      topology_pair_t {
        current_topology,
        final_topology },
      topology_metadata_t {
        final_topology,
        get_newly_enabled_devices_from_topology(current_topology, final_topology),
        primary_device_requested,
        duplicated_devices }
    };
  }

}  // namespace display_device
