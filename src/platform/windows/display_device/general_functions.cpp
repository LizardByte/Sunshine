// standard includes
#include <unordered_set>

// local includes
#include "src/logging.h"
#include "windows_utils.h"

namespace display_device {

  std::string
  get_display_name(const std::string &device_id) {
    if (device_id.empty()) {
      // Valid return, no error
      return {};
    }

    const auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
    if (!display_data) {
      // Error already logged
      return {};
    }

    const auto path { w_utils::get_active_path(device_id, display_data->paths) };
    if (!path) {
      // Debug level, because inactive device is valid case for this function
      BOOST_LOG(debug) << "Failed to find device for " << device_id << "!";
      return {};
    }

    const auto display_name { w_utils::get_display_name(*path) };
    if (display_name.empty()) {
      BOOST_LOG(error) << "Device " << device_id << " has no display name assigned.";
    }

    return display_name;
  }

  bool
  is_primary_device(const std::string &device_id) {
    if (device_id.empty()) {
      BOOST_LOG(error) << "Device id is empty!";
      return false;
    }

    auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
    if (!display_data) {
      // Error already logged
      return false;
    }

    const auto path { w_utils::get_active_path(device_id, display_data->paths) };
    if (!path) {
      BOOST_LOG(error) << "Failed to find device for " << device_id << "!";
      return false;
    }

    const auto source_mode { w_utils::get_source_mode(w_utils::get_source_index(*path, display_data->modes), display_data->modes) };
    if (!source_mode) {
      BOOST_LOG(error) << "Active device does not have a source mode: " << device_id << "!";
      return false;
    }

    return w_utils::is_primary(*source_mode);
  }

  bool
  set_as_primary_device(const std::string &device_id) {
    if (device_id.empty()) {
      BOOST_LOG(error) << "Device id is empty!";
      return false;
    }

    auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
    if (!display_data) {
      // Error already logged
      return false;
    }

    // Get the current origin point of the device (the one that we want to make primary)
    POINTL origin;
    {
      const auto path { w_utils::get_active_path(device_id, display_data->paths) };
      if (!path) {
        BOOST_LOG(error) << "Failed to find device for " << device_id << "!";
        return false;
      }

      const auto source_mode { w_utils::get_source_mode(w_utils::get_source_index(*path, display_data->modes), display_data->modes) };
      if (!source_mode) {
        BOOST_LOG(error) << "Active device does not have a source mode: " << device_id << "!";
        return false;
      }

      if (w_utils::is_primary(*source_mode)) {
        BOOST_LOG(debug) << "Device " << device_id << " is already a primary device.";
        return true;
      }

      origin = source_mode->position;
    }

    // Without verifying if the paths are valid or not (SetDisplayConfig will verify for us),
    // shift their source mode origin points accordingly, so that the provided
    // device moves to (0, 0) position and others to their new positions.
    std::unordered_set<UINT32> modified_modes;
    for (auto &path : display_data->paths) {
      const auto current_id { w_utils::get_device_id(path) };
      const auto source_index { w_utils::get_source_index(path, display_data->modes) };
      auto source_mode { w_utils::get_source_mode(source_index, display_data->modes) };

      if (!source_index || !source_mode) {
        BOOST_LOG(error) << "Active device does not have a source mode: " << current_id << "!";
        return false;
      }

      if (modified_modes.find(*source_index) != std::end(modified_modes)) {
        // Happens when VIRTUAL_MODE_AWARE is not specified when querying paths, probably will never happen in our case, but just to be safe...
        BOOST_LOG(debug) << "Device " << current_id << " shares the same mode index as a previous device. Device is duplicated. Skipping.";
        continue;
      }

      source_mode->position.x -= origin.x;
      source_mode->position.y -= origin.y;

      modified_modes.insert(*source_index);
    }

    const UINT32 flags { SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE | SDC_VIRTUAL_MODE_AWARE };
    const LONG result { SetDisplayConfig(display_data->paths.size(), display_data->paths.data(), display_data->modes.size(), display_data->modes.data(), flags) };
    if (result != ERROR_SUCCESS) {
      BOOST_LOG(error) << w_utils::get_error_string(result) << " failed to set primary mode for " << device_id << "!";
      return false;
    }

    return true;
  }

}  // namespace display_device
