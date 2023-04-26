/**
 * @file system_tray.h
 */

#ifndef SUNSHINE_SYSTEM_TRAY_H
#define SUNSHINE_SYSTEM_TRAY_H

// system_tray namespace
namespace system_tray {

  void
  open_url(const std::string &url);
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

}  // namespace system_tray

#endif // SUNSHINE_SYSTEM_TRAY_H
