/**
 * @file src/system_tray.h
 * @brief Declarations for the system tray icon and notification system.
 */
#pragma once

/**
 * @brief Handles the system tray icon and notification system.
 */
namespace system_tray {
  /**
   * @brief Callback for opening the UI from the system tray.
   * @param item The tray menu item.
   */
  void tray_open_ui_cb(struct tray_menu *item);

  /**
   * @brief Callback for opening GitHub Sponsors from the system tray.
   * @param item The tray menu item.
   */
  void tray_donate_github_cb(struct tray_menu *item);

  /**
   * @brief Callback for opening Patreon from the system tray.
   * @param item The tray menu item.
   */
  void tray_donate_patreon_cb(struct tray_menu *item);

  /**
   * @brief Callback for opening PayPal donation from the system tray.
   * @param item The tray menu item.
   */
  void tray_donate_paypal_cb(struct tray_menu *item);

  /**
   * @brief Callback for resetting display device configuration.
   * @param item The tray menu item.
   */
  void tray_reset_display_device_config_cb(struct tray_menu *item);

  /**
   * @brief Callback for restarting Sunshine from the system tray.
   * @param item The tray menu item.
   */
  void tray_restart_cb(struct tray_menu *item);

  /**
   * @brief Callback for exiting Sunshine from the system tray.
   * @param item The tray menu item.
   */
  void tray_quit_cb(struct tray_menu *item);

  /**
   * @brief Create the system tray.
   * @details This function has an endless loop, so it should be run in a separate thread.
   * @return 1 if the system tray failed to create, otherwise 0 once the tray has been terminated.
   */
  int system_tray();

  /**
   * @brief Run the system tray with platform specific options.
   * @todo macOS requires that UI elements be created on the main thread, so the system tray is not currently implemented for macOS.
   */
  int run_tray();

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
}  // namespace system_tray
