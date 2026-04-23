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
  #elif defined(__linux__) || defined(linux) || defined(__linux) || defined(__FreeBSD__)
    #define TRAY_ICON SUNSHINE_TRAY_PREFIX "-tray"
    #define TRAY_ICON_PLAYING SUNSHINE_TRAY_PREFIX "-playing"
    #define TRAY_ICON_PAUSING SUNSHINE_TRAY_PREFIX "-pausing"
    #define TRAY_ICON_LOCKED SUNSHINE_TRAY_PREFIX "-locked"
  #elif defined(__APPLE__) || defined(__MACH__)
    #define TRAY_ICON WEB_DIR "images/logo-sunshine-16.png"
    #define TRAY_ICON_PLAYING WEB_DIR "images/sunshine-playing-16.png"
    #define TRAY_ICON_PAUSING WEB_DIR "images/sunshine-pausing-16.png"
    #define TRAY_ICON_LOCKED WEB_DIR "images/sunshine-locked-16.png"
    #include <CoreFoundation/CoreFoundation.h>
    #include <dispatch/dispatch.h>
    #include <unordered_map>
  #endif

  // standard includes
  #include <atomic>
  #include <chrono>
  #include <csignal>
  #include <format>
  #include <mutex>
  #include <string>
  #include <thread>
  #include <utility>

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
  static std::atomic tray_initialized = false;

  namespace detail {
    // Holds the shared state used by the async tray update workers. Packaged
    // in a Meyers-singleton accessor below so the mutex and the persistent
    // string buffers are function-local statics rather than file-scope
    // globals.
    struct tray_async_state_t {
      // Serializes mutation of the `tray` struct across the detached workers
      // spawned by update_tray_*(). tray_update() itself is internally
      // serialized on Linux/macOS via the tray library's own main-loop
      // dispatch, but we can still race on the fields of `tray` without this.
      std::mutex mutex;

      // Persistent string storage for notification text. tray_update() on
      // Linux stores pointers into these strings and needs them to stay
      // valid until the next update, so they cannot be local to the worker.
      // Accessed only while holding `mutex`.
      std::string playing_msg;
      std::string pausing_msg;
      std::string stopped_msg;
    };

    static tray_async_state_t &tray_async_state() {
      static tray_async_state_t state;
      return state;
    }

    // Runs `fn` under the tray-async lock and contains any exception it
    // throws so the detached worker thread terminates cleanly instead of
    // propagating exceptions past std::jthread. Templated so the caller
    // passes its lambda directly without a std::function hop.
    template<typename F>
    void safe_run_tray_update(F &&fn) noexcept {
      try {
        auto &state = tray_async_state();
        std::lock_guard lk(state.mutex);
        if (!tray_initialized) {
          return;
        }
        std::forward<F>(fn)();
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Tray update threw: "sv << e.what();
      }
    }
  }  // namespace detail

  // Spawn a detached worker that performs a tray update.
  //
  // Rationale: on Linux, tray_update() synchronously waits for the GTK
  // main loop (i.e. the tray thread) to run the update callback — which
  // in turn calls libnotify and libayatana-appindicator. If the active
  // notification daemon is unresponsive (a common failure mode on
  // Wayland compositors during desktop transitions or when the daemon
  // crashes), the callback blocks indefinitely and the caller blocks
  // with it.
  //
  // The caller is frequently stream::session::join(), which arms a
  // 10-second NVENC-deadlock watchdog that triggers debug_trap() on
  // timeout. A hung notification daemon therefore ends up terminating
  // the entire sunshine process with SIGTRAP. See #4199.
  //
  // Running the update on a detached thread decouples the caller from
  // the tray subsystem: session teardown completes promptly, while
  // the worker stays blocked on tray_update() until the notification
  // daemon eventually responds (or the process exits).
  //
  // Templated on `F` so the caller's lambda is forwarded directly into
  // the worker without a std::function type-erasure hop.
  template<typename F>
  void run_tray_async(F &&fn) {
    if (!tray_initialized) {
      return;
    }
    try {
      // std::jthread is used for its RAII guarantees; the thread is
      // immediately detached because tray_update() is allowed to block
      // indefinitely on a hung notification daemon and must not delay
      // process shutdown or the caller's critical path.
      // Intentional detach: the worker may block inside tray_update()
      // indefinitely if the notification daemon is unresponsive (that is
      // precisely the failure mode this function exists to isolate from
      // the caller). Joining or extending the thread's scope would
      // reintroduce the deadlock.
      std::jthread worker([fn = std::forward<F>(fn)]() mutable {
        detail::safe_run_tray_update(std::move(fn));
      });
      worker.detach();  // NOSONAR(cpp:S5962)
    } catch (const std::system_error &e) {
      // std::jthread construction can fail with std::system_error if the
      // OS is out of resources; in that case we drop the update rather
      // than surface the failure to the caller's critical path.
      BOOST_LOG(warning) << "Failed to spawn tray update thread: "sv << e.what();
    }
  }

  void tray_open_ui_cb([[maybe_unused]] struct tray_menu *item) {
    BOOST_LOG(info) << "Opening UI from system tray"sv;
    launch_ui();
  }

  void tray_donate_github_cb([[maybe_unused]] struct tray_menu *item) {
    platf::open_url("https://github.com/sponsors/LizardByte");
  }

  void tray_donate_patreon_cb([[maybe_unused]] struct tray_menu *item) {
    platf::open_url("https://www.patreon.com/LizardByte");
  }

  void tray_donate_paypal_cb([[maybe_unused]] struct tray_menu *item) {
    platf::open_url("https://www.paypal.com/paypalme/ReenigneArcher");
  }

  void tray_reset_display_device_config_cb([[maybe_unused]] struct tray_menu *item) {
    BOOST_LOG(info) << "Resetting display device config from system tray"sv;

    std::ignore = display_device::reset_persistence();
  }

  void tray_restart_cb([[maybe_unused]] struct tray_menu *item) {
    BOOST_LOG(info) << "Restarting from system tray"sv;

    platf::restart();
  }

  void tray_quit_cb([[maybe_unused]] struct tray_menu *item) {
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

  const char *GetResourcePath(const char *relativePath) {
  #ifdef __APPLE__
    if (!relativePath || !*relativePath) {
      return nullptr;
    }

    // Simple cache ensures our string pointers live forever
    static std::unordered_map<std::string, std::string> g_cache;
    auto search = g_cache.find(relativePath);
    if (search != g_cache.end()) {
      return search->second.c_str();
    }

    // If we're running from an .app bundle, get the internal Resources dir
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle) {
      return relativePath;
    }

    CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(bundle);
    if (!resourcesURL) {
      return relativePath;
    }

    char resourcesPath[PATH_MAX];
    bool ok = CFURLGetFileSystemRepresentation(
      resourcesURL,
      true,
      reinterpret_cast<UInt8 *>(resourcesPath),
      sizeof(resourcesPath)
    );
    CFRelease(resourcesURL);
    if (!ok) {
      return relativePath;
    }

    std::string full;
    if (relativePath && relativePath[0] == '/') {
      full = relativePath;
    } else {
      full = std::string(resourcesPath) + "/" + relativePath;
    }

    BOOST_LOG(debug) << "System Tray: using " << full << " for icon path";

    auto [it, inserted] = g_cache.emplace(relativePath, std::move(full));
    return it->second.c_str();
  #else
    return relativePath;
  #endif
  }

  int init_tray() {
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

  #ifdef __APPLE__
    // if these icon paths are relative, resolve to internal .app Resources path
    tray.allIconPaths[0] = GetResourcePath(TRAY_ICON);
    tray.allIconPaths[1] = GetResourcePath(TRAY_ICON_LOCKED);
    tray.allIconPaths[2] = GetResourcePath(TRAY_ICON_PLAYING);
    tray.allIconPaths[3] = GetResourcePath(TRAY_ICON_PAUSING);

    tray.icon = tray.allIconPaths[0];
  #endif

    if (tray_init(&tray) < 0) {
      BOOST_LOG(warning) << "Failed to create system tray"sv;
      return 1;
    }

    BOOST_LOG(info) << "System tray created"sv;
    tray_initialized = true;
    return 0;
  }

  int process_tray_events() {
    if (!tray_initialized) {
      BOOST_LOG(error) << "System tray is not initialized"sv;
      return 1;
    }

    // Block until an event is processed or tray_quit() is called
    return tray_loop(1);
  }

  int end_tray() {
    if (tray_initialized) {
      tray_initialized = false;
      tray_exit();
    }
    return 0;
  }

  void update_tray_playing(std::string app_name) {
    run_tray_async([name = std::move(app_name)]() {
      auto &state = detail::tray_async_state();
      tray.notification_title = nullptr;
      tray.notification_text = nullptr;
      tray.notification_cb = nullptr;
      tray.notification_icon = nullptr;
      tray.icon = TRAY_ICON_PLAYING;
      tray_update(&tray);

      state.playing_msg = std::format("Streaming started for {}", name);
      tray.icon = TRAY_ICON_PLAYING;
      tray.notification_title = "Stream Started";
      tray.notification_text = state.playing_msg.c_str();
      tray.tooltip = state.playing_msg.c_str();
      tray.notification_icon = TRAY_ICON_PLAYING;
      tray_update(&tray);
    });
  }

  void update_tray_pausing(std::string app_name) {
    run_tray_async([name = std::move(app_name)]() {
      auto &state = detail::tray_async_state();
      tray.notification_title = nullptr;
      tray.notification_text = nullptr;
      tray.notification_cb = nullptr;
      tray.notification_icon = nullptr;
      tray.icon = TRAY_ICON_PAUSING;
      tray_update(&tray);

      state.pausing_msg = std::format("Streaming paused for {}", name);
      tray.icon = TRAY_ICON_PAUSING;
      tray.notification_title = "Stream Paused";
      tray.notification_text = state.pausing_msg.c_str();
      tray.tooltip = state.pausing_msg.c_str();
      tray.notification_icon = TRAY_ICON_PAUSING;
      tray_update(&tray);
    });
  }

  void update_tray_stopped(std::string app_name) {
    run_tray_async([name = std::move(app_name)]() {
      auto &state = detail::tray_async_state();
      tray.notification_title = nullptr;
      tray.notification_text = nullptr;
      tray.notification_cb = nullptr;
      tray.notification_icon = nullptr;
      tray.icon = TRAY_ICON;
      tray_update(&tray);

      state.stopped_msg = std::format("Application {} successfully stopped", name);
      tray.icon = TRAY_ICON;
      tray.notification_icon = TRAY_ICON;
      tray.notification_title = "Application Stopped";
      tray.notification_text = state.stopped_msg.c_str();
      tray.tooltip = PROJECT_NAME;
      tray_update(&tray);
    });
  }

  void update_tray_require_pin() {
    run_tray_async([]() {
      tray.notification_title = nullptr;
      tray.notification_text = nullptr;
      tray.notification_cb = nullptr;
      tray.notification_icon = nullptr;
      tray.icon = TRAY_ICON;
      tray_update(&tray);

      tray.icon = TRAY_ICON;
      tray.notification_title = "Incoming Pairing Request";
      tray.notification_text = "Click here to complete the pairing process";
      tray.notification_icon = TRAY_ICON_LOCKED;
      tray.tooltip = PROJECT_NAME;
      tray.notification_cb = []() {
        launch_ui("/pin");
      };
      tray_update(&tray);
    });
  }

  // Threading functions available on all platforms
  static void tray_thread_worker() {
    platf::set_thread_name("system_tray");
    BOOST_LOG(info) << "System tray thread started"sv;

    // Initialize the tray in this thread
    if (init_tray() != 0) {
      BOOST_LOG(error) << "Failed to initialize tray in thread"sv;
      return;
    }

    // Main tray event loop
    while (process_tray_events() == 0);

    BOOST_LOG(info) << "System tray thread ended"sv;
  }

  int init_tray_threaded() {
    try {
      auto tray_thread = std::thread(tray_thread_worker);

      // The tray thread doesn't require strong lifetime management.
      // It will exit asynchronously when tray_exit() is called.
      tray_thread.detach();

      BOOST_LOG(info) << "System tray thread initialized successfully"sv;
      return 0;
    } catch (const std::exception &e) {
      BOOST_LOG(error) << "Failed to create tray thread: " << e.what();
      return 1;
    }
  }

}  // namespace system_tray
#endif
