/**
 * @file src/platform/windows/nvprefs/nvprefs_common.cpp
 * @brief Definitions for common nvidia preferences.
 */
// this include
#include "nvprefs_common.h"

// local includes
#include "src/config.h"
#include "src/logging.h"

namespace nvprefs {

  void info_message(const std::wstring &message) {
    BOOST_LOG(info) << "nvprefs: " << message;
  }

  void info_message(const std::string &message) {
    BOOST_LOG(info) << "nvprefs: " << message;
  }

  void error_message(const std::wstring &message) {
    BOOST_LOG(error) << "nvprefs: " << message;
  }

  void error_message(const std::string &message) {
    BOOST_LOG(error) << "nvprefs: " << message;
  }

  nvprefs_options get_nvprefs_options() {
    nvprefs_options options;
    options.opengl_vulkan_on_dxgi = config::video.nv_opengl_vulkan_on_dxgi;
    options.sunshine_high_power_mode = config::video.nv_sunshine_high_power_mode;
    return options;
  }

}  // namespace nvprefs
