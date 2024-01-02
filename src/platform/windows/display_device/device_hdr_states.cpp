// local includes
#include "src/display_device/to_string.h"
#include "src/logging.h"
#include "windows_utils.h"

namespace display_device {

  namespace {

    /**
     * @see set_hdr_states for a description as this was split off to reduce cognitive complexity.
     */
    bool
    do_set_states(const hdr_state_map_t &states) {
      const auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
      if (!display_data) {
        // Error already logged
        return false;
      }

      for (const auto &[device_id, state] : states) {
        const auto path { w_utils::get_active_path(device_id, display_data->paths) };
        if (!path) {
          BOOST_LOG(error) << "Failed to find device for " << device_id << "!";
          return false;
        }

        if (state == hdr_state_e::unknown) {
          // We cannot change state to unknown, so we are just ignoring these entries
          // for convenience.
          continue;
        }

        const auto current_state { w_utils::get_hdr_state(*path) };
        if (current_state == hdr_state_e::unknown) {
          BOOST_LOG(error) << "HDR state cannot be changed for " << device_id << "!";
          return false;
        }

        if (!w_utils::set_hdr_state(*path, state == hdr_state_e::enabled)) {
          // Error already logged
          return false;
        }
      }

      return true;
    };

  }  // namespace

  hdr_state_map_t
  get_current_hdr_states(const std::unordered_set<std::string> &device_ids) {
    if (device_ids.empty()) {
      BOOST_LOG(error) << "Device id set is empty!";
      return {};
    }

    const auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
    if (!display_data) {
      // Error already logged
      return {};
    }

    hdr_state_map_t states;
    for (const auto &device_id : device_ids) {
      const auto path { w_utils::get_active_path(device_id, display_data->paths) };
      if (!path) {
        BOOST_LOG(error) << "Failed to find device for " << device_id << "!";
        return {};
      }

      states[device_id] = w_utils::get_hdr_state(*path);
    }

    return states;
  }

  bool
  set_hdr_states(const hdr_state_map_t &states) {
    if (states.empty()) {
      BOOST_LOG(error) << "States map is empty!";
      return false;
    }

    std::unordered_set<std::string> device_ids;
    for (const auto &[device_id, _] : states) {
      if (!device_ids.insert(device_id).second) {
        // Sanity check since, it's technically not possible with unordered map to have duplicate keys
        BOOST_LOG(error) << "Duplicate device id provided: " << device_id << "!";
        return false;
      }
    }

    const auto original_states { get_current_hdr_states(device_ids) };
    if (original_states.empty()) {
      // Error already logged
      return false;
    }

    if (!do_set_states(states)) {
      do_set_states(original_states);  // return value does not matter
      return false;
    }

    return true;
  }

}  // namespace display_device
