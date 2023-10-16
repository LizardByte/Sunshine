/**
 * @file src/system_tray.h
 * @brief todo
 */

#pragma once

// system_tray namespace
namespace system_tray {

  void
  tray_open_ui_cb(struct tray_menu *item);
  void
  tray_donate_github_cb(struct tray_menu *item);
  void
  tray_donate_mee6_cb(struct tray_menu *item);
  void
  tray_donate_patreon_cb(struct tray_menu *item);
  void
  tray_donate_paypal_cb(struct tray_menu *item);
  void
  tray_quit_cb(struct tray_menu *item);

  int
  system_tray();
  int
  run_tray();
  int
  end_tray();
  void
  update_tray_playing(std::string app_name);
  void
  update_tray_pausing(std::string app_name);
  void
  update_tray_stopped(std::string app_name);
  void
  update_tray_require_pin();

}  // namespace system_tray
