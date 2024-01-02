// local includes
#include "to_string.h"
#include "src/logging.h"

namespace display_device {

  std::string
  to_string(device_state_e value) {
    switch (value) {
      case device_state_e::inactive:
        return "INACTIVE";
      case device_state_e::active:
        return "ACTIVE";
      case device_state_e::primary:
        return "PRIMARY";
      default:
        BOOST_LOG(fatal) << "device_state_e conversion not implemented!";
        return {};
    }
  }

  std::string
  to_string(hdr_state_e value) {
    switch (value) {
      case hdr_state_e::unknown:
        return "UNKNOWN";
      case hdr_state_e::disabled:
        return "DISABLED";
      case hdr_state_e::enabled:
        return "ENABLED";
      default:
        BOOST_LOG(fatal) << "hdr_state_e conversion not implemented!";
        return {};
    }
  }

  std::string
  to_string(const hdr_state_map_t &value) {
    std::stringstream output;
    for (const auto &item : value) {
      output << std::endl
             << item.first << " -> " << to_string(item.second);
    }
    return output.str();
  }

  std::string
  to_string(const device_info_t &value) {
    std::stringstream output;
    output << "DISPLAY NAME: " << (value.display_name.empty() ? "NOT AVAILABLE" : value.display_name) << std::endl;
    output << "FRIENDLY NAME: " << (value.friendly_name.empty() ? "NOT AVAILABLE" : value.friendly_name) << std::endl;
    output << "DEVICE STATE: " << to_string(value.device_state) << std::endl;
    output << "HDR STATE: " << to_string(value.hdr_state);
    return output.str();
  }

  std::string
  to_string(const device_info_map_t &value) {
    std::stringstream output;
    bool output_is_empty { true };
    for (const auto &item : value) {
      output << std::endl;
      if (!output_is_empty) {
        output << "-----------------------" << std::endl;
      }

      output << "DEVICE ID: " << item.first << std::endl;
      output << to_string(item.second);
      output_is_empty = false;
    }
    return output.str();
  }

  std::string
  to_string(const resolution_t &value) {
    std::stringstream output;
    output << value.width << "x" << value.height;
    return output.str();
  }

  std::string
  to_string(const refresh_rate_t &value) {
    std::stringstream output;
    if (value.denominator > 0) {
      output << (static_cast<float>(value.numerator) / value.denominator);
    }
    else {
      output << "INF";
    }
    return output.str();
  }

  std::string
  to_string(const display_mode_t &value) {
    std::stringstream output;
    output << to_string(value.resolution) << "x" << to_string(value.refresh_rate);
    return output.str();
  }

  std::string
  to_string(const device_display_mode_map_t &value) {
    std::stringstream output;
    for (const auto &item : value) {
      output << std::endl
             << item.first << " -> " << to_string(item.second);
    }
    return output.str();
  }

  std::string
  to_string(const active_topology_t &value) {
    std::stringstream output;
    bool first_group { true };

    output << std::endl
           << "[" << std::endl;
    for (const auto &group : value) {
      if (!first_group) {
        output << "," << std::endl;
      }
      first_group = false;

      output << "    [" << std::endl;
      bool first_group_item { true };
      for (const auto &group_item : group) {
        if (!first_group_item) {
          output << "," << std::endl;
        }
        first_group_item = false;

        output << "        " << group_item;
      }
      output << std::endl
             << "    ]";
    }
    output << std::endl
           << "]";

    return output.str();
  }

}  // namespace display_device
