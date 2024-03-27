// standard includes
#include <fstream>
#include <thread>

// local includes
#include "settings_topology.h"
#include "src/audio.h"
#include "src/display_device/to_string.h"
#include "src/logging.h"
#include "windows_utils.h"

namespace display_device {

  struct settings_t::persistent_data_t {
    topology_pair_t topology; /**< Contains topology before the modification and the one we modified. */
    std::string original_primary_display; /**< Original primary display in the topology we modified. Empty value if we didn't modify it. */
    device_display_mode_map_t original_modes; /**< Original display modes in the topology we modified. Empty value if we didn't modify it. */
    hdr_state_map_t original_hdr_states; /**< Original display HDR states in the topology we modified. Empty value if we didn't modify it. */

    /**
     * @brief Check if the persistent data contains any meaningful modifications that need to be reverted.
     * @returns True if the data contains something that needs to be reverted, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * settings_t::persistent_data_t data;
     * if (data.contains_modifications()) {
     *   // save persistent data
     * }
     * ```
     */
    [[nodiscard]] bool
    contains_modifications() const {
      return !is_topology_the_same(topology.initial, topology.modified) ||
             !original_primary_display.empty() ||
             !original_modes.empty() ||
             !original_hdr_states.empty();
    }

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(persistent_data_t, topology, original_primary_display, original_modes, original_hdr_states)
  };

  struct settings_t::audio_data_t {
    /**
     * @brief A reference to the audio context that will automatically extend the audio session.
     * @note It is auto-initialized here for convenience.
     */
    decltype(audio::get_audio_ctx_ref()) audio_ctx_ref { audio::get_audio_ctx_ref() };
  };

  namespace {

    /**
     * @brief Get one of the primary display ids found in the topology metadata.
     * @param metadata Topology metadata that also includes current active topology.
     * @return Device id for the primary device, or empty string if primary device not found somehow.
     *
     * EXAMPLES:
     * ```cpp
     * topology_metadata_t metadata;
     * const std::string primary_device_id = get_current_primary_display(metadata);
     * ```
     */
    std::string
    get_current_primary_display(const topology_metadata_t &metadata) {
      for (const auto &group : metadata.current_topology) {
        for (const auto &device_id : group) {
          if (is_primary_device(device_id)) {
            return device_id;
          }
        }
      }

      return std::string {};
    }

    /**
     * @brief Compute the new primary display id based on the information we have.
     * @param original_primary_display Original device id (the one before our first modification or from current topology).
     * @param metadata The current metadata that we are evaluating.
     * @return Primary display id that matches the requirements.
     *
     * EXAMPLES:
     * ```cpp
     * topology_metadata_t metadata;
     * const std::string primary_device_id = determine_new_primary_display("MY_DEVICE_ID", metadata);
     * ```
     */
    std::string
    determine_new_primary_display(const std::string &original_primary_display, const topology_metadata_t &metadata) {
      if (metadata.primary_device_requested) {
        // Primary device was requested - no device was specified by user.
        // This means we are keeping whatever display we have.
        return original_primary_display;
      }

      // For primary devices it is enough to set 1 as a primary display, as the whole duplicated group
      // will become primary displays.
      const auto new_primary_device { metadata.duplicated_devices.front() };
      return new_primary_device;
    }

    /**
     * @brief Change the primary display based on the configuration and previously configured primary display.
     *
     * The function performs the necessary steps for changing the primary display if needed.
     * It also evaluates for possible changes in the configuration and undoes the changes
     * we have made before.
     *
     * @param device_prep Device preparation value from the configuration.
     * @param previous_primary_display Device id of the original primary display we have initially changed (can be empty).
     * @param metadata Additional data with info about the current topology.
     * @return Device id to be used when reverting all settings (can be empty string), or an empty optional if the function fails.
     */
    boost::optional<std::string>
    handle_primary_display_configuration(const parsed_config_t::device_prep_e &device_prep, const std::string &previous_primary_display, const topology_metadata_t &metadata) {
      if (device_prep == parsed_config_t::device_prep_e::ensure_primary) {
        const auto original_primary_display { previous_primary_display.empty() ? get_current_primary_display(metadata) : previous_primary_display };
        const auto new_primary_display { determine_new_primary_display(original_primary_display, metadata) };

        BOOST_LOG(info) << "Changing primary display to: " << new_primary_display;
        if (!set_as_primary_device(new_primary_display)) {
          // Error already logged
          return boost::none;
        }

        // Here we preserve the data from persistence (unless there's none) as in the end that is what we want to go back to.
        return original_primary_display;
      }

      if (!previous_primary_display.empty()) {
        BOOST_LOG(info) << "Changing primary display back to: " << previous_primary_display;
        if (!set_as_primary_device(previous_primary_display)) {
          // Error already logged
          return boost::none;
        }
      }

      return std::string {};
    }

    /**
     * @brief Compute the new display modes based on the information we have.
     * @param resolution Resolution value from the configuration.
     * @param refresh_rate Refresh rate value from the configuration.
     * @param original_display_modes Original display modes (the ones before our first modification or from current topology)
     *                               that we use as a base we will apply changes to.
     * @param metadata The current metadata that we are evaluating.
     * @return New display modes for the topology.
     */
    device_display_mode_map_t
    determine_new_display_modes(const boost::optional<resolution_t> &resolution, const boost::optional<refresh_rate_t> &refresh_rate, const device_display_mode_map_t &original_display_modes, const topology_metadata_t &metadata) {
      device_display_mode_map_t new_modes { original_display_modes };

      if (resolution) {
        // For duplicate devices the resolution must match no matter what, otherwise
        // they cannot be duplicated, which breaks Windows' rules.
        for (const auto &device_id : metadata.duplicated_devices) {
          new_modes[device_id].resolution = *resolution;
        }
      }

      if (refresh_rate) {
        if (metadata.primary_device_requested) {
          // No device has been specified, so if they're all are primary devices
          // we need to apply the refresh rate change to all duplicates
          for (const auto &device_id : metadata.duplicated_devices) {
            new_modes[device_id].refresh_rate = *refresh_rate;
          }
        }
        else {
          // Even if we have duplicate devices, their refresh rate may differ
          // and since the device was specified, let's apply the refresh
          // rate only to the specified device.
          new_modes[metadata.duplicated_devices.front()].refresh_rate = *refresh_rate;
        }
      }

      return new_modes;
    }

    /**
     * @brief Modify the display modes based on the configuration and previously configured display modes.
     *
     * The function performs the necessary steps for changing the display modes if needed.
     * It also evaluates for possible changes in the configuration and undoes the changes
     * we have made before.
     *
     * @param resolution Resolution value from the configuration.
     * @param refresh_rate Refresh rate value from the configuration.
     * @param previous_display_modes Original display modes that we have initially changed (can be empty).
     * @param metadata Additional data with info about the current topology.
     * @return Display modes to be used when reverting all settings (can be empty map), or an empty optional if the function fails.
     */
    boost::optional<device_display_mode_map_t>
    handle_display_mode_configuration(const boost::optional<resolution_t> &resolution, const boost::optional<refresh_rate_t> &refresh_rate, const device_display_mode_map_t &previous_display_modes, const topology_metadata_t &metadata) {
      if (resolution || refresh_rate) {
        const auto original_display_modes { previous_display_modes.empty() ? get_current_display_modes(get_device_ids_from_topology(metadata.current_topology)) : previous_display_modes };
        const auto new_display_modes { determine_new_display_modes(resolution, refresh_rate, original_display_modes, metadata) };

        BOOST_LOG(info) << "Changing display modes to: " << to_string(new_display_modes);
        if (!set_display_modes(new_display_modes)) {
          // Error already logged
          return boost::none;
        }

        // Here we preserve the data from persistence (unless there's none) as in the end that is what we want to go back to.
        return original_display_modes;
      }

      if (!previous_display_modes.empty()) {
        BOOST_LOG(info) << "Changing display modes back to: " << to_string(previous_display_modes);
        if (!set_display_modes(previous_display_modes)) {
          // Error already logged
          return boost::none;
        }
      }

      return device_display_mode_map_t {};
    }

    /**
     * @brief Reverse ("blank") HDR states for newly enabled devices.
     *
     * Some newly enabled displays do not handle HDR state correctly (IDD HDR display for example).
     * The colors can become very blown out/high contrast. A simple workaround is to toggle the HDR state
     * once the display has "settled down" or something.
     *
     * This is what this function does, it changes the HDR state to the opposite states that we will have in the
     * end, sleeps for a little and then allows us to continue changing HDR states to the final ones.
     *
     * "blank" comes as an inspiration from "vblank" as this function is meant to be used before changing the HDR
     * states to clean up something.
     *
     * @param states Final states for the devices that we want to blank.
     * @param newly_enabled_devices Devices to perform blanking for.
     * @return False if the function has failed to set HDR states, true otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * hdr_state_map_t new_states;
     * const bool success = blank_hdr_states(new_states, { "DEVICE_ID" });
     * ```
     */
    bool
    blank_hdr_states(const hdr_state_map_t &states, const std::unordered_set<std::string> &newly_enabled_devices) {
      const std::chrono::milliseconds delay { 1500 };
      if (delay > std::chrono::milliseconds::zero()) {
        bool state_changed { false };
        auto toggled_states { states };
        for (const auto &device_id : newly_enabled_devices) {
          auto state_it { toggled_states.find(device_id) };
          if (state_it == std::end(toggled_states)) {
            continue;
          }

          if (state_it->second == hdr_state_e::enabled) {
            state_it->second = hdr_state_e::disabled;
            state_changed = true;
          }
          else if (state_it->second == hdr_state_e::disabled) {
            state_it->second = hdr_state_e::enabled;
            state_changed = true;
          }
        }

        if (state_changed) {
          BOOST_LOG(debug) << "Toggling HDR states for newly enabled devices and waiting for " << delay.count() << "ms before actually applying the correct states.";
          if (!set_hdr_states(toggled_states)) {
            // Error already logged
            return false;
          }

          std::this_thread::sleep_for(delay);
        }
      }

      return true;
    }

    /**
     * @brief Compute the new HDR states based on the information we have.
     * @param change_hdr_state HDR state value from the configuration.
     * @param original_hdr_states Original HDR states (the ones before our first modification or from current topology)
     *                            that we use as a base we will apply changes to.
     * @param metadata The current metadata that we are evaluating.
     * @return New HDR states for the topology.
     */
    hdr_state_map_t
    determine_new_hdr_states(const boost::optional<bool> &change_hdr_state, const hdr_state_map_t &original_hdr_states, const topology_metadata_t &metadata) {
      hdr_state_map_t new_states { original_hdr_states };

      if (change_hdr_state) {
        const hdr_state_e final_state { *change_hdr_state ? hdr_state_e::enabled : hdr_state_e::disabled };
        const auto try_update_new_state = [&new_states, final_state](const std::string &device_id) {
          const auto current_state { new_states[device_id] };
          if (current_state == hdr_state_e::unknown) {
            return;
          }

          new_states[device_id] = final_state;
        };

        if (metadata.primary_device_requested) {
          // No device has been specified, so if they're all are primary devices
          // we need to apply the HDR state change to all duplicates
          for (const auto &device_id : metadata.duplicated_devices) {
            try_update_new_state(device_id);
          }
        }
        else {
          // Even if we have duplicate devices, their HDR states may differ
          // and since the device was specified, let's apply the HDR state
          // only to the specified device.
          try_update_new_state(metadata.duplicated_devices.front());
        }
      }

      return new_states;
    }

    /**
     * @brief Modify the display HDR states based on the configuration and previously configured display HDR states.
     *
     * The function performs the necessary steps for changing the display HDR states if needed.
     * It also evaluates for possible changes in the configuration and undoes the changes
     * we have made before.
     *
     * @param change_hdr_state HDR state value from the configuration.
     * @param previous_hdr_states Original display HDR states have initially changed (can be empty).
     * @param metadata Additional data with info about the current topology.
     * @return Display HDR states to be used when reverting all settings (can be empty map), or an empty optional if the function fails.
     */
    boost::optional<hdr_state_map_t>
    handle_hdr_state_configuration(const boost::optional<bool> &change_hdr_state, const hdr_state_map_t &previous_hdr_states, const topology_metadata_t &metadata) {
      if (change_hdr_state) {
        const auto original_hdr_states { previous_hdr_states.empty() ? get_current_hdr_states(get_device_ids_from_topology(metadata.current_topology)) : previous_hdr_states };
        const auto new_hdr_states { determine_new_hdr_states(change_hdr_state, original_hdr_states, metadata) };

        BOOST_LOG(info) << "Changing hdr states to: " << to_string(new_hdr_states);
        if (!blank_hdr_states(new_hdr_states, metadata.newly_enabled_devices) || !set_hdr_states(new_hdr_states)) {
          // Error already logged
          return boost::none;
        }

        // Here we preserve the data from persistence (unless there's none) as in the end that is what we want to go back to.
        return original_hdr_states;
      }

      if (!previous_hdr_states.empty()) {
        BOOST_LOG(info) << "Changing hdr states back to: " << to_string(previous_hdr_states);
        if (!blank_hdr_states(previous_hdr_states, metadata.newly_enabled_devices) || !set_hdr_states(previous_hdr_states)) {
          // Error already logged
          return boost::none;
        }
      }

      return hdr_state_map_t {};
    }

    /**
     * @brief Revert settings to the ones found in the persistent data.
     * @param data Reference to persistent data containing original settings.
     * @param data_modified Reference to a boolean that is set to true if changes are made to the persistent data reference.
     * @return True if all settings within persistent data have been reverted, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * bool data_modified { false };
     * settings_t::persistent_data_t data;
     *
     * if (!try_revert_settings(data, data_modified)) {
     *   if (data_modified) {
     *     // Update the persistent file
     *   }
     * }
     * ```
     */
    bool
    try_revert_settings(settings_t::persistent_data_t &data, bool &data_modified) {
      try {
        nlohmann::json json_data = data;
        BOOST_LOG(debug) << "Reverting persistent display settings from:\n"
                         << json_data.dump(4);
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to dump persistent display settings: " << err.what();
      }

      if (!data.contains_modifications()) {
        return true;
      }

      const bool have_changes_for_modified_topology { !data.original_primary_display.empty() || !data.original_modes.empty() || !data.original_hdr_states.empty() };
      std::unordered_set<std::string> newly_enabled_devices;
      bool partially_failed { false };
      auto current_topology { get_current_topology() };

      if (have_changes_for_modified_topology) {
        if (set_topology(data.topology.modified)) {
          newly_enabled_devices = get_newly_enabled_devices_from_topology(current_topology, data.topology.modified);
          current_topology = data.topology.modified;

          if (!data.original_hdr_states.empty()) {
            BOOST_LOG(info) << "Changing back the HDR states to: " << to_string(data.original_hdr_states);
            if (set_hdr_states(data.original_hdr_states)) {
              data.original_hdr_states.clear();
              data_modified = true;
            }
            else {
              partially_failed = true;
            }
          }

          if (!data.original_modes.empty()) {
            BOOST_LOG(info) << "Changing back the display modes to: " << to_string(data.original_modes);
            if (set_display_modes(data.original_modes)) {
              data.original_modes.clear();
              data_modified = true;
            }
            else {
              partially_failed = true;
            }
          }

          if (!data.original_primary_display.empty()) {
            BOOST_LOG(info) << "Changing back the primary device to: " << data.original_primary_display;
            if (set_as_primary_device(data.original_primary_display)) {
              data.original_primary_display.clear();
              data_modified = true;
            }
            else {
              partially_failed = true;
            }
          }
        }
        else {
          BOOST_LOG(error) << "Cannot switch to the topology to undo changes!";
          partially_failed = true;
        }
      }

      BOOST_LOG(info) << "Changing display topology back to: " << to_string(data.topology.initial);
      if (set_topology(data.topology.initial)) {
        newly_enabled_devices.merge(get_newly_enabled_devices_from_topology(current_topology, data.topology.initial));
        current_topology = data.topology.initial;
        data_modified = true;
      }
      else {
        BOOST_LOG(error) << "Failed to switch back to the initial topology!";
        partially_failed = true;
      }

      if (!newly_enabled_devices.empty()) {
        const auto current_hdr_states { get_current_hdr_states(get_device_ids_from_topology(current_topology)) };

        BOOST_LOG(debug) << "Trying to fix HDR states (if needed).";
        blank_hdr_states(current_hdr_states, newly_enabled_devices);  // Return value ignored
        set_hdr_states(current_hdr_states);  // Return value ignored
      }

      return !partially_failed;
    }

    /**
     * @brief Save settings to the JSON file.
     * @param filepath Filepath for the persistent data.
     * @param data Persistent data to save.
     * @return True if the filepath is empty or the data was saved to the file, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * settings_t::persistent_data_t data;
     *
     * if (save_settings("/foo/bar.json", data)) {
     *   // Do stuff...
     * }
     * ```
     */
    bool
    save_settings(const std::filesystem::path &filepath, const settings_t::persistent_data_t &data) {
      if (filepath.empty()) {
        BOOST_LOG(warning) << "No filename was specified for persistent display device configuration.";
        return true;
      }

      try {
        std::ofstream file(filepath, std::ios::out | std::ios::trunc);
        nlohmann::json json_data = data;

        // Write json with indentation
        file << std::setw(4) << json_data << std::endl;
        BOOST_LOG(debug) << "Saved persistent display settings:\n"
                         << json_data.dump(4);
        return true;
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to save display settings: " << err.what();
      }

      return false;
    }

    /**
     * @brief Load persistent data from the JSON file.
     * @param filepath Filepath to load data from.
     * @return Unique pointer to the persistent data if it was loaded successfully, nullptr otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * auto data = load_settings("/foo/bar.json");
     * ```
     */
    std::unique_ptr<settings_t::persistent_data_t>
    load_settings(const std::filesystem::path &filepath) {
      try {
        if (!filepath.empty() && std::filesystem::exists(filepath)) {
          std::ifstream file(filepath);
          return std::make_unique<settings_t::persistent_data_t>(nlohmann::json::parse(file));
        }
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to load saved display settings: " << err.what();
      }

      return nullptr;
    }

    /**
     * @brief Remove the file.
     * @param filepath Filepath to remove.
     *
     * EXAMPLES:
     * ```cpp
     * remove_file("/foo/bar.json");
     * ```
     */
    void
    remove_file(const std::filesystem::path &filepath) {
      try {
        if (!filepath.empty()) {
          std::filesystem::remove(filepath);
        }
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to remove " << filepath << ". Error: " << err.what();
      }
    }

  }  // namespace

  settings_t::settings_t() = default;

  settings_t::~settings_t() = default;

  bool
  settings_t::is_changing_settings_going_to_fail() const {
    return w_utils::is_user_session_locked() || w_utils::test_no_access_to_ccd_api();
  }

  settings_t::apply_result_t
  settings_t::apply_config(const parsed_config_t &config) {
    const auto do_apply_config { [this](const parsed_config_t &config) -> settings_t::apply_result_t {
      bool failed_while_reverting_settings { false };
      const boost::optional<topology_pair_t> previously_configured_topology { persistent_data ? boost::make_optional(persistent_data->topology) : boost::none };

      // On Windows the display settings are kept per an active topology list - each topology
      // has separate configuration saved in the database. Therefore, we must always switch
      // to the topology we want to modify before we actually start applying settings.
      const auto topology_result { handle_device_topology_configuration(config, previously_configured_topology, [&]() {
        const bool audio_sink_was_captured { audio_data != nullptr };
        if (!revert_settings()) {
          failed_while_reverting_settings = true;
          return false;
        }

        if (audio_sink_was_captured && !audio_data) {
          audio_data = std::make_unique<audio_data_t>();
        }
        return true;
      }) };
      if (!topology_result) {
        // Error already logged
        return { failed_while_reverting_settings ? apply_result_t::result_e::revert_fail : apply_result_t::result_e::topology_fail };
      }

      // Once we have switched to the correct topology, we need to select where we want to
      // save persistent data.
      //
      // If we already have cached persistent data, we want to use that, however we must NOT
      // take over the topology "pair" from the result as the initial topology doest not
      // reflect the actual initial topology before we made our first changes.
      //
      // There is no better way to somehow always guess the initial topology we want to revert to.
      // The user could have switched topology when the stream was paused, then technically we could
      // try to switch back to that topology. However, the display could have also turned off and the
      // topology was automatically changed by Windows. In this case we don't want to switch back to
      // that topology since it was not the user's decision.
      //
      // Therefore, we are always sticking with the first initial topology before the first configuration
      // was applied.
      persistent_data_t new_settings { topology_result->pair };
      persistent_data_t &current_settings { persistent_data ? *persistent_data : new_settings };

      const auto persist_settings = [&]() -> apply_result_t {
        if (current_settings.contains_modifications()) {
          if (!persistent_data) {
            persistent_data = std::make_unique<persistent_data_t>(new_settings);
          }

          if (!save_settings(filepath, *persistent_data)) {
            return { apply_result_t::result_e::file_save_fail };
          }
        }
        else if (persistent_data) {
          if (!revert_settings()) {
            // Sanity check, as the revert_settings should always pass
            // at this point since our settings contain no modifications.
            return { apply_result_t::result_e::revert_fail };
          }
        }

        return { apply_result_t::result_e::success };
      };

      // Since we will be modifying system state in multiple steps, we
      // have no choice, but to save any changes we have made so
      // that we can undo them if anything fails.
      auto save_guard = util::fail_guard([&]() {
        persist_settings();  // Ignoring the return value
      });

      // Here each of the handler returns full set of their specific settings for
      // all the displays in the topology.
      //
      // We have the same train of though here as with the topology - if we are
      // controlling some parts of the display settings, we are taking what
      // we have before any modification by us are sticking with it until we
      // release the control.
      //
      // Also, since we keep settings for all the displays (not only the ones that
      // we modify), we can use these settings as a base that will revert whatever
      // we did before if we are re-applying settings with different configuration.
      //
      // User modified the resolution manually? Well, he shouldn't have. If we
      // are responsible for the resolution, then hands off! Initial settings
      // will be re-applied when the paused session is resumed.

      const auto original_primary_display { handle_primary_display_configuration(config.device_prep, current_settings.original_primary_display, topology_result->metadata) };
      if (!original_primary_display) {
        // Error already logged
        return { apply_result_t::result_e::primary_display_fail };
      }
      current_settings.original_primary_display = *original_primary_display;

      const auto original_modes { handle_display_mode_configuration(config.resolution, config.refresh_rate, current_settings.original_modes, topology_result->metadata) };
      if (!original_modes) {
        // Error already logged
        return { apply_result_t::result_e::modes_fail };
      }
      current_settings.original_modes = *original_modes;

      const auto original_hdr_states { handle_hdr_state_configuration(config.change_hdr_state, current_settings.original_hdr_states, topology_result->metadata) };
      if (!original_hdr_states) {
        // Error already logged
        return { apply_result_t::result_e::hdr_states_fail };
      }
      current_settings.original_hdr_states = *original_hdr_states;

      save_guard.disable();
      return persist_settings();
    } };

    BOOST_LOG(info) << "Applying configuration to the display device.";
    const bool display_may_change { config.device_prep == parsed_config_t::device_prep_e::ensure_only_display };
    if (display_may_change && !audio_data) {
      // It is very likely that in this situation our "current" audio device will be gone, so we
      // want to capture the audio sink immediately and extend the audio session until we revert our changes.
      BOOST_LOG(debug) << "Capturing audio sink before changing display";
      audio_data = std::make_unique<audio_data_t>();
    }

    const auto result { do_apply_config(config) };
    if (result) {
      if (!display_may_change && audio_data) {
        // Just to be safe in the future when the video config can be reloaded
        // without Sunshine restarting, we should clean up, because in this situation
        // we have had to revert the changes that turned off other displays. Thus, extending
        // the session for a display that again exist is pointless.
        BOOST_LOG(debug) << "Releasing captured audio sink";
        audio_data = nullptr;
      }
    }

    if (!result) {
      BOOST_LOG(error) << "Failed to configure display:\n"
                       << result.get_error_message();
    }
    else {
      BOOST_LOG(info) << "Display device configuration applied.";
    }
    return result;
  }

  bool
  settings_t::revert_settings() {
    if (!persistent_data) {
      BOOST_LOG(info) << "Loading persistent display device settings.";
      persistent_data = load_settings(filepath);
    }

    if (persistent_data) {
      BOOST_LOG(info) << "Reverting display device settings.";

      bool data_updated { false };
      if (!try_revert_settings(*persistent_data, data_updated)) {
        if (data_updated) {
          save_settings(filepath, *persistent_data);  // Ignoring return value
        }

        BOOST_LOG(error) << "Failed to revert display device settings!";
        return false;
      }

      remove_file(filepath);
      persistent_data = nullptr;

      if (audio_data) {
        BOOST_LOG(debug) << "Releasing captured audio sink";
        audio_data = nullptr;
      }

      BOOST_LOG(info) << "Display device configuration reverted.";
    }

    return true;
  }

  void
  settings_t::reset_persistence() {
    BOOST_LOG(info) << "Purging persistent display device data (trying to reset settings one last time).";
    if (persistent_data && !revert_settings()) {
      BOOST_LOG(info) << "Failed to revert settings - proceeding to reset persistence.";
    }

    remove_file(filepath);
    persistent_data = nullptr;

    if (audio_data) {
      BOOST_LOG(debug) << "Releasing captured audio sink";
      audio_data = nullptr;
    }
  }

}  // namespace display_device
