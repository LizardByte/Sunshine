/**
 * @file src/platform/windows/nvprefs/nvprefs_common.cpp
 * @brief Definitions for common nvidia preferences.
 */
// this include
#include "nvprefs_common.h"

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/windows/utf_utils.h"

namespace nvprefs {

  /**
   * @brief Forward an informational NVPrefs message to the logger.
   */
  void info_message(const std::wstring &message) {
    logging::info([&message](std::ostream &out) {
      out << "nvprefs: " << utf_utils::to_utf8(message);
    });
  }

  /**
   * @brief Forward an informational NVPrefs message to the logger.
   */
  void info_message(const std::string &message) {
    logging::info("nvprefs: {}", message);
  }

  /**
   * @brief Forward an NVPrefs error message to the logger.
   */
  void error_message(const std::wstring &message) {
    logging::error([&message](std::ostream &out) {
      out << "nvprefs: " << utf_utils::to_utf8(message);
    });
  }

  /**
   * @brief Forward an NVPrefs error message to the logger.
   */
  void error_message(const std::string &message) {
    logging::error("nvprefs: {}", message);
  }

  /**
   * @brief Get nvprefs options.
   */
  nvprefs_options get_nvprefs_options(const config::settings_t &settings) {
    nvprefs_options options;
    options.opengl_vulkan_on_dxgi = settings.video.nv_opengl_vulkan_on_dxgi;
    options.sunshine_high_power_mode = settings.video.nv_sunshine_high_power_mode;
    return options;
  }

}  // namespace nvprefs
