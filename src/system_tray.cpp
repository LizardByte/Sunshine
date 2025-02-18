/**
 * @file src/system_tray.cpp
 * @brief Definitions for the system tray icon and notification system.
 */
// macros
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1

  #if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <accctrl.h>
    #include <aclapi.h>
    #include <windows.h>
    #define TRAY_ICON WEB_DIR "images/sunshine.ico"
    #define TRAY_ICON_PLAYING WEB_DIR "images/sunshine-playing.ico"
    #define TRAY_ICON_PAUSING WEB_DIR "images/sunshine-pausing.ico"
    #define TRAY_ICON_LOCKED WEB_DIR "images/sunshine-locked.ico"
  #elif defined(__linux__) || defined(linux) || defined(__linux)
    #define TRAY_ICON "sunshine-tray"
    #define TRAY_ICON_PLAYING "sunshine-playing"
    #define TRAY_ICON_PAUSING "sunshine-pausing"
    #define TRAY_ICON_LOCKED "sunshine-locked"
  #elif defined(__APPLE__) || defined(__MACH__)
    #define TRAY_ICON WEB_DIR "images/logo-sunshine-16.png"
    #define TRAY_ICON_PLAYING WEB_DIR "images/sunshine-playing-16.png"
    #define TRAY_ICON_PAUSING WEB_DIR "images/sunshine-pausing-16.png"
    #define TRAY_ICON_LOCKED WEB_DIR "images/sunshine-locked-16.png"
    #include <dispatch/dispatch.h>
  #endif

  // standard includes
  #include <csignal>
  #include <string>

  // lib includes
  #include "tray/src/tray.h"
  #include <boost/filesystem.hpp>
  #include <boost/process/v1/environment.hpp>

  // local includes
  #include "confighttp.h"
  #include "display_device/session.h"
  #include "logging.h"
  #include "platform/common.h"
  #include "process.h"
  #include "src/display_device/display_device.h"
  #include "src/entry_handler.h"
  #include "version.h"
  #include <chrono>
  #include <future>
  #include <thread>

using namespace std::literals;

// system_tray namespace
namespace system_tray {
  static std::atomic<bool> tray_initialized = false;

  // 前向声明所有回调函数
  void tray_open_ui_cb(struct tray_menu *item);
  void tray_toggle_display_cb(struct tray_menu *item);
  void tray_reset_display_device_config_cb(struct tray_menu *item);
  void tray_restart_cb(struct tray_menu *item);
  void tray_quit_cb(struct tray_menu *item);

  // 菜单数组声明
  static struct tray_menu tray_menus[] = {
    { .text = "Open Sunshine", .cb = tray_open_ui_cb },
    { .text = "-" },
    { .text = "VDD Monitor Toggle", .checked = 0, .cb = tray_toggle_display_cb },
    // { .text = "Donate",
    //   .submenu =
    //     (struct tray_menu[]) {
    //       { .text = "GitHub Sponsors", .cb = tray_donate_github_cb },
    //       { .text = "Patreon", .cb = tray_donate_patreon_cb },
    //       { .text = "PayPal", .cb = tray_donate_paypal_cb },
    //       { .text = nullptr } } },
    { .text = "-" },
  #ifdef _WIN32
    { .text = "Reset Display Device Config", .cb = tray_reset_display_device_config_cb },
  #endif
    { .text = "Restart", .cb = tray_restart_cb },
    { .text = "Quit", .cb = tray_quit_cb },
    { .text = nullptr }
  };

  static struct tray tray = {
    .icon = TRAY_ICON,
    .tooltip = PROJECT_NAME,
    .menu = tray_menus,
    .iconPathCount = 4,
    .allIconPaths = { TRAY_ICON, TRAY_ICON_LOCKED, TRAY_ICON_PLAYING, TRAY_ICON_PAUSING },
  };

  void
  tray_open_ui_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Opening UI from system tray"sv;
    launch_ui();
  }

  void
  tray_donate_github_cb(struct tray_menu *item) {
    platf::open_url("https://github.com/sponsors/LizardByte");
  }

  void
  tray_donate_patreon_cb(struct tray_menu *item) {
    platf::open_url("https://www.patreon.com/LizardByte");
  }

  void
  tray_donate_paypal_cb(struct tray_menu *item) {
    platf::open_url("https://www.paypal.com/paypalme/ReenigneArcher");
  }

  void
  tray_reset_display_device_config_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Resetting display device config from system tray"sv;

    display_device::session_t::get().reset_persistence();
  }

  void
  tray_restart_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Restarting from system tray"sv;

    platf::restart();
  }

  void
  tray_quit_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Quitting from system tray"sv;

  #ifdef _WIN32
    int msgboxID = MessageBoxW(
      NULL,
      L"你不能退出!\n那么想退吗? 真拿你没办法呢, 继续点一下吧~",
      L" 真的要退出吗",
      MB_ICONWARNING | MB_OKCANCEL);

    if (msgboxID == IDOK) {
      // If we're running in a service, return a special status to
      // tell it to terminate too, otherwise it will just respawn us.
      if (GetConsoleWindow() == NULL) {
        lifetime::exit_sunshine(ERROR_SHUTDOWN_IN_PROGRESS, true);
        return;
      }
    }
  #endif

    lifetime::exit_sunshine(0, true);
  }

  class AsyncTask {
  public:
    ~AsyncTask() { 
        // 添加超时处理,避免无限等待
        if (fut.valid()) {
            auto status = fut.wait_for(std::chrono::seconds(1));
            if (status != std::future_status::ready) {
                BOOST_LOG(warning) << "AsyncTask timeout on destruction";
            }
        }
    }
    
    void launch(std::function<void()> task) {
        try {
            // 确保之前的任务已完成
            if (fut.valid()) {
                auto status = fut.wait_for(std::chrono::seconds(1));
                if (status != std::future_status::ready) {
                    BOOST_LOG(warning) << "Previous task timeout, forcing new task";
                }
            }
            fut = std::async(std::launch::async, task);
        }
        catch (const std::exception& e) {
            BOOST_LOG(error) << "Failed to launch async task: " << e.what();
        }
    }
  private:
    std::future<void> fut;
  };

  static AsyncTask async_task;

  void
  tray_toggle_display_cb(struct tray_menu *item) {
    // 添加状态检查和日志
    if (!tray_initialized) {
        BOOST_LOG(warning) << "Tray not initialized, ignoring toggle";
        return;
    }
    
    if (system_tray::tray_menus[2].disabled) {
        BOOST_LOG(info) << "Toggle display is in cooldown, ignoring request";
        return;
    }

    BOOST_LOG(info) << "Toggling display power from system tray"sv;
    display_device::session_t::get().toggle_display_power();
    
    // 添加10秒禁用状态
    system_tray::tray_menus[2].disabled = 1;
    tray_update(&tray);
    
    async_task.launch([]{
      std::this_thread::sleep_for(10s);
      system_tray::tray_menus[2].disabled = 0;
      tray_update(&tray);
    });
  }

  int
  system_tray() {
  #ifdef _WIN32
    // If we're running as SYSTEM, Explorer.exe will not have permission to open our thread handle
    // to monitor for thread termination. If Explorer fails to open our thread, our tray icon
    // will persist forever if we terminate unexpectedly. To avoid this, we will modify our thread
    // DACL to add an ACE that allows SYNCHRONIZE access to Everyone.
    {
      PACL old_dacl;
      PSECURITY_DESCRIPTOR sd;
      auto error = GetSecurityInfo(GetCurrentThread(),
        SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &old_dacl,
        nullptr,
        &sd);
      if (error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "GetSecurityInfo() failed: "sv << error;
        return 1;
      }

      auto free_sd = util::fail_guard([sd]() {
        LocalFree(sd);
      });

      SID_IDENTIFIER_AUTHORITY sid_authority = SECURITY_WORLD_SID_AUTHORITY;
      PSID world_sid;
      if (!AllocateAndInitializeSid(&sid_authority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &world_sid)) {
        error = GetLastError();
        BOOST_LOG(warning) << "AllocateAndInitializeSid() failed: "sv << error;
        return 1;
      }

      auto free_sid = util::fail_guard([world_sid]() {
        FreeSid(world_sid);
      });

      EXPLICIT_ACCESS ea {};
      ea.grfAccessPermissions = SYNCHRONIZE;
      ea.grfAccessMode = GRANT_ACCESS;
      ea.grfInheritance = NO_INHERITANCE;
      ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      ea.Trustee.ptstrName = (LPSTR) world_sid;

      PACL new_dacl;
      error = SetEntriesInAcl(1, &ea, old_dacl, &new_dacl);
      if (error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "SetEntriesInAcl() failed: "sv << error;
        return 1;
      }

      auto free_new_dacl = util::fail_guard([new_dacl]() {
        LocalFree(new_dacl);
      });

      error = SetSecurityInfo(GetCurrentThread(),
        SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        new_dacl,
        nullptr);
      if (error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "SetSecurityInfo() failed: "sv << error;
        return 1;
      }
    }

    // Wait for the shell to be initialized before registering the tray icon.
    // This ensures the tray icon works reliably after a logoff/logon cycle.
    while (GetShellWindow() == nullptr) {
      Sleep(1000);
    }
  #endif

    if (tray_init(&tray) < 0) {
      BOOST_LOG(warning) << "Failed to create system tray"sv;
      return 1;
    }
    else {
      BOOST_LOG(info) << "System tray created"sv;
    }

    // 初始化时获取实际显示器状态
    tray_menus[2].checked = display_device::session_t::get().is_display_on() ? 1 : 0;
    tray_update(&tray);

    tray_initialized = true;
    while (tray_loop(1) == 0) {
      BOOST_LOG(debug) << "System tray loop"sv;
    }

    return 0;
  }

  void
  run_tray() {
    // create the system tray
  #if defined(__APPLE__) || defined(__MACH__)
    // macOS requires that UI elements be created on the main thread
    // creating tray using dispatch queue does not work, although the code doesn't actually throw any (visible) errors

    // dispatch_async(dispatch_get_main_queue(), ^{
    //   system_tray();
    // });

    BOOST_LOG(info) << "system_tray() is not yet implemented for this platform."sv;
  #else  // Windows, Linux
    // create tray in separate thread
    std::thread tray_thread(system_tray);
    tray_thread.detach();
  #endif
  }

  int
  end_tray() {
    tray_initialized = false;
    tray_exit();
    return 0;
  }

  void
  update_tray_playing(std::string app_name) {
    if (!tray_initialized) {
      return;
    }

    tray.notification_title = NULL;
    tray.notification_text = NULL;
    tray.notification_cb = NULL;
    tray.notification_icon = NULL;
    tray.icon = TRAY_ICON_PLAYING;
    tray_update(&tray);
    tray.icon = TRAY_ICON_PLAYING;
    tray.notification_title = "Stream Started";
    char msg[256];
    snprintf(msg, std::size(msg), "Streaming started for %s", app_name.c_str());
    tray.notification_text = msg;
    tray.tooltip = msg;
    tray.notification_icon = TRAY_ICON_PLAYING;
    tray_update(&tray);
  }

  void
  update_tray_pausing(std::string app_name) {
    if (!tray_initialized) {
      return;
    }

    tray.notification_title = NULL;
    tray.notification_text = NULL;
    tray.notification_cb = NULL;
    tray.notification_icon = NULL;
    tray.icon = TRAY_ICON_PAUSING;
    tray_update(&tray);
    char msg[256];
    snprintf(msg, std::size(msg), "Streaming paused for %s", app_name.c_str());
    tray.icon = TRAY_ICON_PAUSING;
    tray.notification_title = "Stream Paused";
    tray.notification_text = msg;
    tray.tooltip = msg;
    tray.notification_icon = TRAY_ICON_PAUSING;
    tray_update(&tray);
  }

  void
  update_tray_stopped(std::string app_name) {
    if (!tray_initialized) {
      return;
    }

    tray.notification_title = NULL;
    tray.notification_text = NULL;
    tray.notification_cb = NULL;
    tray.notification_icon = NULL;
    tray.icon = TRAY_ICON;
    tray_update(&tray);
    char msg[256];
    snprintf(msg, std::size(msg), "Application %s successfully stopped", app_name.c_str());
    tray.icon = TRAY_ICON;
    tray.notification_icon = TRAY_ICON;
    tray.notification_title = "Application Stopped";
    tray.notification_text = msg;
    tray.tooltip = PROJECT_NAME;
    tray_update(&tray);
  }

  void
  update_tray_require_pin(std::string pin_name) {
    if (!tray_initialized) {
      return;
    }

    tray.notification_title = NULL;
    tray.notification_text = NULL;
    tray.notification_cb = NULL;
    tray.notification_icon = NULL;
    tray.icon = TRAY_ICON;
    tray_update(&tray);
    tray.icon = TRAY_ICON;
    std::string title = "Incoming Pairing Request From: " + pin_name;
    tray.notification_title = title.c_str();
    tray.notification_text = "Click here to complete the pairing process";
    tray.notification_icon = TRAY_ICON_LOCKED;
    tray.tooltip = pin_name.c_str();
    tray.notification_cb = []() {
      launch_ui_with_path("/pin");
    };
    tray_update(&tray);
  }

  void
  update_tray_vmonitor_checked(int checked) {
    if (!tray_initialized) {
      return;
    }
    // 更新显示器切换菜单项的勾选状态
    tray_menus[2].checked = checked;
    // 同时更新禁用状态（冷却期间保持禁用）
    tray_menus[2].disabled = checked ? 0 : tray_menus[2].disabled;
    tray_update(&tray);
  }

}  // namespace system_tray
#endif
