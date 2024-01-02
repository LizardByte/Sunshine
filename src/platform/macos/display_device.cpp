// local includes
#include "src/display_device/settings.h"

namespace display_device {

  device_info_map_t
  enum_available_devices() {
    // Not implemented
    return {};
  }

  std::string
  get_display_name(const std::string &value) {
    // Not implemented, but just passthrough the value
    return value;
  }

  device_display_mode_map_t
  get_current_display_modes(const std::unordered_set<std::string> &) {
    // Not implemented
    return {};
  }

  bool
  set_display_modes(const device_display_mode_map_t &) {
    // Not implemented
    return false;
  }

  bool
  is_primary_device(const std::string &) {
    // Not implemented
    return false;
  }

  bool
  set_as_primary_device(const std::string &) {
    // Not implemented
    return false;
  }

  hdr_state_map_t
  get_current_hdr_states(const std::unordered_set<std::string> &) {
    // Not implemented
    return {};
  }

  bool
  set_hdr_states(const hdr_state_map_t &) {
    // Not implemented
    return false;
  }

  active_topology_t
  get_current_topology() {
    // Not implemented
    return {};
  }

  bool
  is_topology_valid(const active_topology_t &topology) {
    // Not implemented
    return false;
  }

  bool
  is_topology_the_same(const active_topology_t &a, const active_topology_t &b) {
    // Not implemented
    return false;
  }

  bool
  set_topology(const active_topology_t &) {
    // Not implemented
    return false;
  }

  struct settings_t::audio_data_t {
    // Not implemented
  };

  struct settings_t::persistent_data_t {
    // Not implemented
  };

  settings_t::settings_t() {
    // Not implemented
  }

  settings_t::~settings_t() {
    // Not implemented
  }

  bool
  settings_t::is_changing_settings_going_to_fail() const {
    // Not implemented
    return false;
  }

  settings_t::apply_result_t
  settings_t::apply_config(const parsed_config_t &) {
    // Not implemented
    return { apply_result_t::result_e::success };
  }

  bool
  settings_t::revert_settings() {
    // Not implemented
    return true;
  }

  void
  settings_t::reset_persistence() {
    // Not implemented
  }

}  // namespace display_device
