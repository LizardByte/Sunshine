/**
 * @file src/platform/windows/vdd_control.h
 * @brief Declarations for Parsec Virtual Display Driver control.
 */
#pragma once

// standard includes
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace vdd {

  /**
   * @brief Information about a single virtual display.
   */
  struct DisplayInfo {
    int index;       ///< Display index (0-based)
    int identifier;  ///< Display identifier
    int width;       ///< Current width
    int height;      ///< Current height
    int hz;          ///< Current refresh rate
    std::string device_name;  ///< Windows device name (e.g. \\.\\DISPLAY1)
  };

  /**
   * @brief Status of the VDD driver.
   */
  enum class DriverStatus {
    OK,               ///< Driver is ready
    NOT_INSTALLED,    ///< Parsec VDD driver is not installed
    DISABLED,         ///< Driver device is disabled
    RESTART_REQUIRED, ///< System restart is required
    INACCESSIBLE,     ///< Driver cannot be accessed
    UNKNOWN           ///< Unknown driver status
  };

  /**
   * @brief Initialize the VDD driver connection.
   * @return true if initialization was successful.
   */
  bool init();

  /**
   * @brief Clean up and close the VDD driver handle.
   */
  void destroy();

  /**
   * @brief Check whether we are initialized.
   */
  bool is_initialized();

  /**
   * @brief Get the current driver status.
   */
  DriverStatus get_driver_status();

  /**
   * @brief Get the driver version.
   * @return Version string, e.g. "0.45".
   */
  std::string get_driver_version();

  /**
   * @brief Check if there are any physical (non-VDD) displays attached.
   * @return true if only VDD displays exist or no displays at all.
   */
  bool need_virtual_display();

  /**
   * @brief Add a virtual display.
   * @return The display index, or -1 on failure.
   */
  int add_display(int width, int height, int hz);

  /**
   * @brief Remove a virtual display by index.
   */
  bool remove_display(int index);

  /**
   * @brief Remove the last added virtual display.
   */
  bool remove_last_display();

  /**
   * @brief Remove all virtual displays.
   */
  void remove_all_displays();

  /**
   * @brief Get a list of currently active virtual displays.
   */
  std::vector<DisplayInfo> get_displays();

  /**
   * @brief Get the number of currently active virtual displays.
   */
  int get_display_count();

  /**
   * @brief Start the keepalive thread that periodically pings the driver.
   */
  void start_keepalive();

  /**
   * @brief Stop the keepalive thread.
   */
  void stop_keepalive();

}  // namespace vdd
