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
    #define TRAY_ICON WEB_DIR "images/sunshine.ico"
    #define TRAY_ICON_PLAYING WEB_DIR "images/sunshine-playing.ico"
    #define TRAY_ICON_PAUSING WEB_DIR "images/sunshine-pausing.ico"
    #define TRAY_ICON_LOCKED WEB_DIR "images/sunshine-locked.ico"
  #elif defined(__linux__) || defined(linux) || defined(__linux)
    #define TRAY_ICON SUNSHINE_TRAY_PREFIX "-tray"
    #define TRAY_ICON_PLAYING SUNSHINE_TRAY_PREFIX "-playing"
    #define TRAY_ICON_PAUSING SUNSHINE_TRAY_PREFIX "-pausing"
    #define TRAY_ICON_LOCKED SUNSHINE_TRAY_PREFIX "-locked"
  #elif defined(__APPLE__) || defined(__MACH__)
    #define TRAY_ICON WEB_DIR "images/logo-sunshine-16.png"
    #define TRAY_ICON_PLAYING WEB_DIR "images/sunshine-playing-16.png"
    #define TRAY_ICON_PAUSING WEB_DIR "images/sunshine-pausing-16.png"
    #define TRAY_ICON_LOCKED WEB_DIR "images/sunshine-locked-16.png"
    #include <dispatch/dispatch.h>
  #endif

  // standard includes
  #include <atomic>
  #include <chrono>
  #include <condition_variable>
  #include <csignal>
  #include <deque>
  #include <functional>
  #include <future>
  #include <mutex>
  #include <string>
  #include <thread>

  // lib includes
  #include <boost/filesystem.hpp>
  #include <boost/process/v1/environment.hpp>
  #include <tray/src/tray.h>

  // local includes
  #include "confighttp.h"
  #include "display_device.h"
  #include "logging.h"
  #include "platform/common.h"
  #include "process.h"
  #include "src/entry_handler.h"

using namespace std::literals;

// system_tray namespace
namespace system_tray {
  static std::atomic<bool> tray_initialized = false;
  static std::atomic<bool> notifications_disabled = false;
  static std::mutex notify_q_mutex;
  static std::condition_variable notify_q_cv;
  static std::deque<std::function<void()>> notify_q;
  static std::thread notify_worker;
  static std::once_flag notify_start_once;  // ensure worker is started once
  static std::atomic<bool> notify_worker_should_stop = false;
  static std::atomic<bool> notify_error_logged = false;
  static std::mutex tray_mutex;
  static std::string g_tooltip;
  static std::string g_notification_title;
  static std::string g_notification_text;

  static void log_notifications_disabled_once() {
    bool expected = false;
    if (notify_error_logged.compare_exchange_strong(expected, true)) {
  #if defined(__linux__)
      BOOST_LOG(error) << "Notifications disabled due to errors (notification call exceeded 3s). Is a desktop notification service installed?"sv;
  #else
      BOOST_LOG(error) << "Notifications disabled due to errors (notification call exceeded 3s)."sv;
  #endif
    }
  }

  static void start_notify_worker_if_needed();
  static void stop_notify_worker();

  // Enqueue a notification job to be executed on the worker thread. If notifications are
  // disabled, log an error and drop the job without blocking the caller.
  static void enqueue_notify_job(std::function<void()> fn) {
    if (!tray_initialized) {
      return;
    }
    if (notifications_disabled.load(std::memory_order_relaxed)) {
      log_notifications_disabled_once();
      return;
    }
    start_notify_worker_if_needed();
    {
      std::lock_guard<std::mutex> lk(notify_q_mutex);
      notify_q.emplace_back(std::move(fn));
    }
    notify_q_cv.notify_one();
  }

  void tray_open_ui_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Opening UI from system tray"sv;
    launch_ui();
  }

  void tray_donate_github_cb(struct tray_menu *item) {
    platf::open_url("https://github.com/sponsors/LizardByte");
  }

  void tray_donate_patreon_cb(struct tray_menu *item) {
    platf::open_url("https://www.patreon.com/LizardByte");
  }

  void tray_donate_paypal_cb(struct tray_menu *item) {
    platf::open_url("https://www.paypal.com/paypalme/ReenigneArcher");
  }

  void tray_reset_display_device_config_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Resetting display device config from system tray"sv;

    std::ignore = display_device::reset_persistence();
  }

  void tray_restart_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Restarting from system tray"sv;

    platf::restart();
  }

  void tray_quit_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Quitting from system tray"sv;

  #ifdef _WIN32
    // If we're running in a service, return a special status to
    // tell it to terminate too, otherwise it will just respawn us.
    if (GetConsoleWindow() == nullptr) {
      lifetime::exit_sunshine(ERROR_SHUTDOWN_IN_PROGRESS, true);
      return;
    }
  #endif

    lifetime::exit_sunshine(0, true);
  }

  // Tray menu
  static struct tray tray = {
    .icon = TRAY_ICON,
    .tooltip = PROJECT_NAME,
    .menu =
      (struct tray_menu[]) {
        // todo - use boost/locale to translate menu strings
        {.text = "Open Sunshine", .cb = tray_open_ui_cb},
        {.text = "-"},
        {.text = "Donate",
         .submenu =
           (struct tray_menu[]) {
             {.text = "GitHub Sponsors", .cb = tray_donate_github_cb},
             {.text = "Patreon", .cb = tray_donate_patreon_cb},
             {.text = "PayPal", .cb = tray_donate_paypal_cb},
             {.text = nullptr}
           }},
        {.text = "-"},
  // Currently display device settings are only supported on Windows
  #ifdef _WIN32
        {.text = "Reset Display Device Config", .cb = tray_reset_display_device_config_cb},
  #endif
        {.text = "Restart", .cb = tray_restart_cb},
        {.text = "Quit", .cb = tray_quit_cb},
        {.text = nullptr}
      },
    .iconPathCount = 4,
    .allIconPaths = {TRAY_ICON, TRAY_ICON_LOCKED, TRAY_ICON_PLAYING, TRAY_ICON_PAUSING},
  };

  int system_tray() {
  #ifdef _WIN32
    // If we're running as SYSTEM, Explorer.exe will not have permission to open our thread handle
    // to monitor for thread termination. If Explorer fails to open our thread, our tray icon
    // will persist forever if we terminate unexpectedly. To avoid this, we will modify our thread
    // DACL to add an ACE that allows SYNCHRONIZE access to Everyone.
    {
      PACL old_dacl;
      PSECURITY_DESCRIPTOR sd;
      auto error = GetSecurityInfo(GetCurrentThread(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd);
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

      error = SetSecurityInfo(GetCurrentThread(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl, nullptr);
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
    } else {
      BOOST_LOG(info) << "System tray created"sv;
    }

    tray_initialized = true;
    start_notify_worker_if_needed();
    while (tray_loop(1) == 0) {
      BOOST_LOG(debug) << "System tray loop"sv;
    }

    return 0;
  }

  void run_tray() {
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

  int end_tray() {
    tray_initialized = false;
    stop_notify_worker();
    {
      std::lock_guard<std::mutex> lk(tray_mutex);
      tray_exit();
    }
    return 0;
  }

  static void set_toast_notification(const std::string &tooltip, const std::string &note, const std::string &title = "", const char *icon = TRAY_ICON, void (*cb)() = nullptr) {
    // To prevent dangling references, we will copy the strings into global variables
    // We only need to do this for the notification, the icon and everything else will generally remain in scope.
    g_tooltip = tooltip;
    g_notification_text = note;
    g_notification_title = title;

    tray.icon = icon;
    tray.tooltip = g_tooltip.c_str();

    if (!notifications_disabled.load(std::memory_order_acquire)) {
      tray.notification_text = g_notification_text.c_str();
      tray.notification_title = g_notification_title.c_str();
      tray.notification_icon = icon; 
      tray.notification_cb = cb;
    } else {
      // Disable notifications: keep safe empties and clear callback
      static const char empty_str[] = "";
      tray.notification_text = empty_str;
      tray.notification_title = empty_str;
      tray.notification_icon = TRAY_ICON;
      tray.notification_cb = nullptr;
    }

    tray_update(&tray);
  }

  static void enqueue_tray_update(const std::string &tooltip, const std::string &note, const std::string &title, const char *icon, void (*cb)() = nullptr) {
    enqueue_notify_job([=]() {
      try {
        std::lock_guard<std::mutex> lk(tray_mutex);
        set_toast_notification(tooltip, note, title, icon, cb);
      } catch (...) {
        BOOST_LOG(error) << "Exception during tray notification update."sv;
      }
    });
  }

  void update_tray_playing(std::string app_name) {
    if (!tray_initialized) {
      return;
    }
    std::string msg = "Streaming started for " + app_name;
    enqueue_tray_update(msg, msg, "Stream Started", TRAY_ICON_PLAYING);
  }

  void update_tray_pausing(std::string app_name) {
    if (!tray_initialized) {
      return;
    }
    std::string msg = "Streaming paused for " + app_name;
    enqueue_tray_update(msg, msg, "Stream Paused", TRAY_ICON_PAUSING);
  }

  void update_tray_stopped(std::string app_name) {
    if (!tray_initialized) {
      return;
    }
    std::string msg = "Application " + app_name + " successfully stopped";
    enqueue_tray_update(PROJECT_NAME, msg, "Application Stopped", TRAY_ICON);
  }

  void update_tray_require_pin() {
    if (!tray_initialized) {
      return;
    }
    enqueue_tray_update(PROJECT_NAME, "Click here to complete the pairing process", "Incoming Pairing Request", TRAY_ICON_LOCKED, []() {
      launch_ui("/pin");
    });
  }

  static void notify_worker_loop() {
    while (!notify_worker_should_stop.load(std::memory_order_relaxed)) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lk(notify_q_mutex);
        notify_q_cv.wait(lk, [] {
          return notify_worker_should_stop.load(std::memory_order_relaxed) || !notify_q.empty();
        });
        if (notify_worker_should_stop.load(std::memory_order_relaxed)) {
          break;
        }
        job = std::move(notify_q.front());
        notify_q.pop_front();
      }

      if (job) {
        // Run the job with a 3-second timeout
        std::packaged_task<void()> task(std::move(job));
        auto fut = task.get_future();
        std::thread t(std::move(task));

        if (fut.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
          // Job is stuck; disable notifications and detach the stuck thread
          notifications_disabled.store(true, std::memory_order_relaxed);
          log_notifications_disabled_once();
          t.detach();
        } else {
          // Finished within timeout
          t.join();
        }
      }
    }
  }

  static void start_notify_worker_if_needed() {
    // Ensure only a single worker is started.
    std::call_once(notify_start_once, [] {
      notify_worker_should_stop.store(false, std::memory_order_relaxed);
      notify_worker = std::thread(notify_worker_loop);
    });
  }

  static void stop_notify_worker() {
    // Only clear the queue once the worker has stopped.
    notify_worker_should_stop.store(true, std::memory_order_relaxed);
    notify_q_cv.notify_all();
    if (notify_worker.joinable()) {
      notify_worker.join();
    }

    // Worker is stopped, clear the notification queue.
    {
      std::lock_guard<std::mutex> lk(notify_q_mutex);
      notify_q.clear();
    }
  }

}  // namespace system_tray
#endif
