/**
 * @file system_tray.cpp
 */
// macros
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1

#if defined(_WIN32) || defined(_WIN64)
#define TRAY_ICON WEB_DIR "images/favicon.ico"
#include <windows.h>
#elif defined(__linux__) || defined(linux) || defined(__linux)
#define TRAY_ICON "sunshine"
#elif defined(__APPLE__) || defined(__MACH__)
#define TRAY_ICON WEB_DIR "images/logo-sunshine-16.png"
#include <dispatch/dispatch.h>
#endif

// standard includes
#include <csignal>
#include <string>

// lib includes
#include "tray/tray.h"

// local includes
#include "confighttp.h"
#include "main.h"

// local includes
//#include "platform/common.h"
//#include "process.h"

using namespace std::literals;

// system_tray namespace
namespace system_tray {

/**
 * @brief Open a url in the default web browser.
 * @param url The url to open.
 */
void open_url(const std::string &url) {
// if windows
#if defined(_WIN32) || defined(_WIN64)
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else // if unix
  system(("open "s + url).c_str());
#endif
}

/**
 * @brief Callback for opening the UI from the system tray.
 * @param item The tray menu item.
 */
void tray_open_ui_cb(struct tray_menu *item) {
  BOOST_LOG(info) << "Opening UI from system tray"sv;

  // create the url with the port
  std::string url = "https://localhost:" + std::to_string(map_port(confighttp::PORT_HTTPS));

  // open the url in the default web browser
  open_url(url);
}

/**
 * @brief Callback for opening GitHub Sponsors from the system tray.
 * @param item The tray menu item.
 */
void tray_donate_github_cb(struct tray_menu *item) {
  open_url("https://github.com/sponsors/LizardByte");
}

/**
 * @brief Callback for opening MEE6 donation from the system tray.
 * @param item The tray menu item.
 */
void tray_donate_mee6_cb(struct tray_menu *item) {
  open_url("https://mee6.xyz/m/804382334370578482");
}

/**
 * @brief Callback for opening Patreon from the system tray.
 * @param item The tray menu item.
 */
void tray_donate_patreon_cb(struct tray_menu *item) {
  open_url("https://www.patreon.com/LizardByte");
}

/**
 * @brief Callback for opening PayPal donation from the system tray.
 * @param item The tray menu item.
 */
void tray_donate_paypal_cb(struct tray_menu *item) {
  open_url("https://www.paypal.com/paypalme/ReenigneArcher");
}

/**
 * @brief Callback for exiting Sunshine from the system tray.
 * @param item The tray menu item.
 */
void tray_quit_cb(struct tray_menu *item) {
  BOOST_LOG(info) << "Quiting from system tray"sv;

  std::raise(SIGINT);
}

// Tray menu
static struct tray tray = {
  .icon = TRAY_ICON,
#if defined(_WIN32) || defined(_WIN64)
  .tooltip = const_cast<char *>("Sunshine"), // cast the string literal to a non-const char* pointer
#endif
  .menu =
    (struct tray_menu[]) {
      // todo - use boost/locale to translate menu strings
      { .text = "Open Sunshine", .cb = tray_open_ui_cb },
      { .text = "-" },
      { .text = "Donate",
        .submenu =
          (struct tray_menu[]) {
            { .text = "GitHub Sponsors", .cb = tray_donate_github_cb },
            { .text = "MEE6", .cb = tray_donate_mee6_cb },
            { .text = "Patreon", .cb = tray_donate_patreon_cb },
            { .text = "PayPal", .cb = tray_donate_paypal_cb },
            { .text = nullptr } } },
      { .text = "-" },
      { .text = "Quit", .cb = tray_quit_cb },
      { .text = nullptr } },
};


/**
 * @brief Create the system tray.
 * @details This function has an endless loop, so it should be run in a separate thread.
 * @return 1 if the system tray failed to create, otherwise 0 once the tray has been terminated.
 */
int system_tray() {
  if(tray_init(&tray) < 0) {
    BOOST_LOG(warning) << "Failed to create system tray"sv;
    return 1;
  }
  else {
    BOOST_LOG(info) << "System tray created"sv;
  }

  while(tray_loop(1) == 0) {
    BOOST_LOG(debug) << "System tray loop"sv;
  }

  return 0;
}

/**
 * @brief Run the system tray with platform specific options.
 * @note macOS requires that UI elements be created on the main thread, so the system tray is not implemented for macOS.
 */
void run_tray() {
  // create the system tray
#if defined(__APPLE__) || defined(__MACH__)
  // macOS requires that UI elements be created on the main thread
  // creating tray using dispatch queue does not work, although the code doesn't actually throw any (visible) errors

  // dispatch_async(dispatch_get_main_queue(), ^{
  //   system_tray();
  // });

  BOOST_LOG(info) << "system_tray() is not yet implemented for this platform."sv;
#else // Windows, Linux
  // create tray in separate thread
  std::thread tray_thread(system_tray);
  tray_thread.detach();
#endif
}

/**
 * @brief Exit the system tray.
 * @return 0 after exiting the system tray.
 */
int end_tray() {
  tray_exit();
  return 0;
}

} // namespace system_tray
#endif
