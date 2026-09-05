/**
 * @file src/system_tray.h
 * @brief Declarations for the system tray icon and notification system.
 */
#pragma once

// standard includes
#include <string>
#include <string_view>

#ifdef _WIN32
namespace lvh {
  struct LicenseStatus;
}
#endif

/**
 * @brief Handles the system tray icon and notification system.
 */
namespace system_tray {
  /**
   * @brief Callback for opening the UI from the system tray.
   * @param item The tray menu item.
   */
  void tray_open_ui_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for opening GitHub Sponsors from the system tray.
   * @param item The tray menu item.
   */
  void tray_donate_github_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for opening Patreon from the system tray.
   * @param item The tray menu item.
   */
  void tray_donate_patreon_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for opening PayPal donation from the system tray.
   * @param item The tray menu item.
   */
  void tray_donate_paypal_cb([[maybe_unused]] struct tray_menu *item);

#ifdef _WIN32
  /**
   * @brief Callback for opening Virtual HID Driver license settings in the Web UI.
   * @param item The tray menu item.
   */
  void tray_virtualhid_license_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for opening the latest Virtual HID Driver release.
   * @param item The tray menu item.
   */
  void tray_virtualhid_download_cb([[maybe_unused]] struct tray_menu *item);
#endif

  /**
   * @brief Callback for resetting display device configuration.
   * @param item The tray menu item.
   */
  void tray_reset_display_device_config_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for restarting Sunshine from the system tray.
   * @param item The tray menu item.
   */
  void tray_restart_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Callback for exiting Sunshine from the system tray.
   * @param item The tray menu item.
   */
  void tray_quit_cb([[maybe_unused]] struct tray_menu *item);

  /**
   * @brief Initializes the system tray without starting a loop.
   * @return 0 if initialization was successful, non-zero otherwise.
   */
  int init_tray();

  /**
   * @brief Processes a single tray event iteration.
   * @return 0 if processing was successful, non-zero otherwise.
   */
  int process_tray_events();

  /**
   * @brief Exit the system tray.
   * @return 0 after exiting the system tray.
   */
  int end_tray();

  /**
   * @brief Sets the tray icon in playing mode and spawns the appropriate notification
   * @param app_name The started application name
   */
  void update_tray_playing(std::string app_name);

  /**
   * @brief Sets the tray icon in pausing mode (stream stopped but app running) and spawns the appropriate notification
   * @param app_name The paused application name
   */
  void update_tray_pausing(std::string app_name);

  /**
   * @brief Sets the tray icon in stopped mode (app and stream stopped) and spawns the appropriate notification
   * @param app_name The started application name
   */
  void update_tray_stopped(std::string app_name);

  /**
   * @brief Spawns a notification for PIN Pairing. Clicking it opens the PIN Web UI Page
   */
  void update_tray_require_pin();

#ifdef _WIN32
  /**
   * @brief Update the Virtual HID Driver license submenu and optional notification.
   *
   * @param license Latest machine license details.
   * @param notify_if_unlicensed Whether to notify the user when the machine is not activated.
   */
  void update_tray_virtualhid_license(const lvh::LicenseStatus &license, bool notify_if_unlicensed);

  /**
   * @brief Query the Virtual HID Driver license and prepare the startup tray state.
   */
  void prepare_tray_virtualhid_license();

  /**
   * @brief Show an update notification for an unsupported Virtual HID Driver.
   *
   * Existing notifications are preserved when the driver is absent or supported.
   *
   * @param installed Whether the driver is installed.
   * @param version Installed driver version.
   * @param version_compatible Whether Sunshine supports the installed version.
   * @param supported_versions User-visible supported version range.
   */
  void update_tray_virtualhid_driver(
    bool installed,
    std::string_view version,
    bool version_compatible,
    std::string_view supported_versions
  );

  /**
   * @brief Query the Virtual HID Driver version and prepare its startup notification.
   */
  void prepare_tray_virtualhid_driver();
#endif

  /**
   * @brief Initializes and runs the system tray in a separate thread.
   * @return 0 if initialization was successful, non-zero otherwise.
   */
  int init_tray_threaded();

#ifdef SUNSHINE_TESTS
  /**
   * @brief Get the tray data used by the system tray implementation.
   *
   * @return Read-only tray data for unit-test assertions.
   */
  const struct tray &tray_data_for_testing();

  /**
   * @brief Check whether the system tray is initialized.
   *
   * @return true if the system tray is initialized; otherwise, false.
   */
  bool tray_initialized_for_testing();

  /**
   * @brief Restore the persistent tray data to its initial state between tests.
   */
  void reset_tray_data_for_testing();

  /**
   * @brief Resolve a tray resource path using the production platform logic.
   *
   * @param relative_path Resource path relative to the executable or application bundle.
   * @return Stable resource path used by the tray backend.
   */
  const char *resource_path_for_testing(const char *relative_path);

  /**
   * @brief Resolve all tray icon paths using the production platform logic.
   */
  void resolve_tray_icon_paths_for_testing();
#endif
}  // namespace system_tray
