/**
 * @file src/main.cpp
 * @brief Definitions for the main entry point for Sunshine.
 */
// standard includes
#include <cerrno>
#include <codecvt>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

// platform includes
#ifdef __APPLE__
  #include "platform/macos/misc.h"

  #include <mach-o/dyld.h>
#endif
#ifdef __linux__
  #include "platform/common.h"
  #include "platform/linux/graphics.h"
  #include "platform/linux/misc.h"

  #include <sys/auxv.h>
#endif

// lib includes
#include <rs.h>
#ifdef _WIN32
  #include <libvirtualhid/license.hpp>
#endif

// local includes
#include "audio.h"
#include "confighttp.h"
#include "display_device.h"
#include "entry_handler.h"
#include "globals.h"
#include "httpcommon.h"
#include "input.h"
#include "logging.h"
#include "main.h"
#include "nvhttp.h"
#include "platform/permissions.h"
#include "process.h"
#include "rtsp.h"
#include "stream.h"
#include "system_tray.h"
#include "upnp.h"
#include "video.h"

using namespace std::literals;

/**
 * @brief Immutable dispatch table for supported command-line subcommands.
 *
 * Callbacks share one dispatch signature and leave unused arguments unnamed.
 */
const std::map<std::string_view, std::function<int(const config::settings_t &settings, const char *name, int argc, char **argv)>> cmd_to_func {
  {"creds"sv, [](const config::settings_t &settings, const char *name, int argc, char **argv) {
     return args::creds(settings, name, argc, argv);
   }},
  {"help"sv, [](const config::settings_t &, const char *name, int, char **) {
     return args::help(name);
   }},
  {"version"sv, [](const config::settings_t &, const char *, int, char **) {
     return args::version();
   }},
#ifdef _WIN32
  {"restore-nvprefs-undo"sv, [](const config::settings_t &, const char *, int, char **) {
     return args::restore_nvprefs_undo();
   }},
#endif
};

#ifdef _WIN32
/**
 * @brief Handle Windows session-change messages for the monitor window.
 *
 * @param hwnd Window handle receiving the Windows control event.
 * @param uMsg U msg.
 * @param wParam W param.
 * @param lParam L param.
 * @return Process or platform callback exit code.
 */
LRESULT CALLBACK SessionMonitorWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_NCCREATE) {
    const auto create = reinterpret_cast<const CREATESTRUCTA *>(lParam);
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  switch (uMsg) {
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_ENDSESSION:
      {
        // Terminate ourselves with a blocking exit call
        std::cout << "Received WM_ENDSESSION"sv << std::endl;
        if (const auto controller = reinterpret_cast<lifetime::controller_t *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA))) {
          controller->exit_sunshine(0, true);
        }
        return 0;
      }
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}

/**
 * @brief Handle Windows console control events for shutdown.
 *
 * @param type Protocol, message, or resource type selector.
 * @return Process or platform callback exit code.
 */
WINAPI BOOL ConsoleCtrlHandler(DWORD type) {
  if (type == CTRL_CLOSE_EVENT) {
    logging::info("Console closed handler called");
    lifetime::signal_guard_t::native_exit(0, false);
  }
  return FALSE;
}
#endif

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
constexpr bool tray_is_enabled = true;  ///< Compile-time flag indicating tray support is enabled.
#else
constexpr bool tray_is_enabled = false;
#endif

/**
 * @brief Run the main event loop until Sunshine is asked to exit.
 *
 * @param settings Application settings used by the event loop.
 * @param shutdown_event Shutdown event.
 */
void mainThreadLoop(const config::settings_t &settings, const std::shared_ptr<safe::event_t<bool>> &shutdown_event) {
  bool run_loop = false;

  // Conditions that would require the main thread event loop
#ifndef _WIN32
  run_loop = tray_is_enabled && settings.sunshine.system_tray;  // On Windows, tray runs in separate thread, so no main loop needed for tray
#endif

  if (!run_loop) {
    logging::info("No main thread features enabled, skipping event loop"sv);
    // Wait for shutdown
    shutdown_event->view();
    return;
  }

  // Main thread event loop
  logging::info("Starting main loop"sv);
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
  system_tray::run_tray_until_exit(shutdown_event);
#endif
  logging::info("Main loop has exited"sv);
}

/**
 * @brief Run the main application or worker loop.
 *
 * @param argc The number of arguments.
 * @param argv The arguments.
 * @return Process or platform callback exit code.
 */
int main(int argc, char *argv[]) {
#ifdef __linux__
  const bool privileged_execution = getauxval(AT_SECURE) != 0 || platf::has_elevated_privileges(true);
  if (privileged_execution && !platf::sanitize_process_environment()) {
    std::cerr << "Failed to sanitize the environment for privileged execution: " << std::strerror(errno) << '\n';
    return 1;
  }
#endif

#ifdef __APPLE__
  // Bundle assets are referenced relative to the executable
  // (e.g. ../Resources/assets), so anchor cwd to Contents/MacOS.
  {
    char executable[2048];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) == 0) {
      std::error_code ec;
      auto exec_dir = std::filesystem::weakly_canonical(std::filesystem::path {executable}, ec).parent_path();
      if (!ec) {
        std::filesystem::current_path(exec_dir, ec);
      }
      if (ec) {
        std::cerr << "Failed to set working directory to executable path: " << ec.message() << '\n';
      }
    }
  }
#endif
#if defined(__linux__)
  // On Linux, child threads inherit capabilities from the parent at creation time.
  // We ensure the privileged worker starts while capabilities are still held,
  // and then immediately strip global privileges from the main thread and future tasks.
  // Please ensure that these calls remain ordered as early as possible in startup initialization.
  #if defined(SUNSHINE_BUILD_DRM)
  platf::kms::ensure_privileged_drm_worker_started();
  #endif
  // Check and drop capabilities but use 'false' to retain CAP_SYS_NICE for EGL high priority contexts
  if (platf::has_elevated_privileges(false)) {
    platf::drop_elevated_privileges(false);
  }

  // Next, initialize privileged EGL worker thread
  egl::ensure_privileged_egl_worker_started();
  // Finally, check and drop all capabilities via 'true', which includes CAP_SYS_NICE.
  if (platf::has_elevated_privileges(true)) {
    platf::drop_elevated_privileges(true);
  }
#endif

  runtime::infrastructure_t infrastructure {argv};
  auto &task_pool = infrastructure.scheduler();
  auto &lifetime = infrastructure.lifetime();
  const auto host_mail = infrastructure.mailbox();
  config::store_t configuration;
  const auto &settings = configuration.values();
#ifdef _WIN32
  auto &nvprefs_instance = infrastructure.preferences();
#endif

  task_pool_util::TaskPool::task_id_t force_shutdown = nullptr;

#ifdef _WIN32
  // Avoid searching the PATH in case a user has configured their system insecurely
  // by placing a user-writable directory in the system-wide PATH variable.
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);

  setlocale(LC_ALL, "C");
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale::global(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
#pragma GCC diagnostic pop

  // parse config file
  if (configuration.parse(argc, argv)) {
    return 0;
  }

  auto log_deinit_guard = logging::init(settings.sunshine.min_log_level, settings.sunshine.log_file);
  if (!log_deinit_guard) {
    logging::error("Logging failed to initialize"sv);
  }

  // logging can begin at this point
  // if anything is logged prior to this point, it will appear in stdout, but not in the log viewer in the UI
  // the version should be printed to the log before anything else
  logging::info("{} version: {} commit: {}", PROJECT_NAME, PROJECT_VERSION, PROJECT_VERSION_COMMIT);

  // Log publisher metadata
  log_publisher_data();

  // Log modified_config_settings
  configuration.log_config_settings(settings.modified_config_settings, false);
  configuration.edit_startup().modified_config_settings.clear();

  if (!settings.sunshine.cmd.name.empty()) {
    auto fn = cmd_to_func.find(settings.sunshine.cmd.name);
    if (fn == std::end(cmd_to_func)) {
      logging::fatal("Unknown command: {}", settings.sunshine.cmd.name);

      logging::info("Possible commands:"sv);
      for (auto &[key, _] : cmd_to_func) {
        logging::info("\t{}", key);
      }

      return 7;
    }

    return fn->second(settings, argv[0], settings.sunshine.cmd.argc, settings.sunshine.cmd.argv);
  }

#ifdef _WIN32
  configuration.select_all_gamepad_drivers_if_licensed(lvh::get_license_status().license.licensed());
#endif

  // Adding guard here first as it also performs recovery after crash,
  // otherwise people could theoretically end up without display output.
  // It also should be destroyed before forced shutdown to expedite the cleanup.
  audio::service_t audios {settings, host_mail};
  display_device::manager_t displays;
  displays.initialize(platf::appdata() / "display_device.state", settings.video, audios);

  http::state_t http_state {configuration};
  if (http_state.initialize()) {
    logging::fatal("HTTP interface failed to initialize"sv);

#ifdef _WIN32
    logging::fatal("To relaunch Sunshine successfully, use the shortcut in the Start Menu. Do not run Sunshine.exe manually."sv);
    std::this_thread::sleep_for(10s);
#endif

    return -1;
  }

#ifdef _WIN32
  // Modify relevant NVIDIA control panel settings if the system has corresponding gpu
  if (nvprefs_instance.load()) {
    // Restore global settings to the undo file left by improper termination of sunshine.exe
    nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
    // Modify application settings for sunshine.exe
    nvprefs_instance.modify_application_profile(settings);
    // Modify global settings, undo file is produced in the process to restore after improper termination
    nvprefs_instance.modify_global_profile(settings);
    // Unload dynamic library to survive driver re-installation
    nvprefs_instance.unload();
  }

#endif

  task_pool.start(1);
  auto scheduler_guard = util::fail_guard([&task_pool]() {
    task_pool.stop();
    task_pool.join();
  });

  // Create signal handler after logging has been initialized
  auto shutdown_event = host_mail->event<bool>(mail::shutdown);
  lifetime::signal_guard_t signals {lifetime};
  signals.on_signal(SIGINT, [&force_shutdown, &displays, &task_pool, &settings, shutdown_event]() {
    logging::info("Interrupt handler called"sv);

    auto task = []() {
      logging::fatal("10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv);
      logging::log_flush();
      lifetime::debug_trap();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    // Break out of the main loop
    shutdown_event->raise(true);

    if (tray_is_enabled && settings.sunshine.system_tray) {
      system_tray::end_tray();
    }

    displays.revert_configuration();
  });

  signals.on_signal(SIGTERM, [&force_shutdown, &displays, &task_pool, &settings, shutdown_event]() {
    logging::info("Terminate handler called"sv);

    auto task = []() {
      logging::fatal("10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv);
      logging::log_flush();
      lifetime::debug_trap();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    // Break out of the main loop
    shutdown_event->raise(true);

    if (tray_is_enabled && settings.sunshine.system_tray) {
      system_tray::end_tray();
    }

    displays.revert_configuration();
  });

#ifdef _WIN32
  // Terminate gracefully on Windows when console window is closed
  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
  auto console_handler_guard = util::fail_guard([]() {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
  });
  // Wait as long as possible to terminate Sunshine.exe during logoff/shutdown
  SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

  // We must create a hidden window to receive shutdown notifications since we load gdi32.dll
  std::promise<HWND> session_monitor_hwnd_promise;
  auto session_monitor_hwnd_future = session_monitor_hwnd_promise.get_future();

  std::jthread session_monitor_thread([&]() {
    platf::set_thread_name("session_monitor");

    WNDCLASSA wnd_class {};
    wnd_class.lpszClassName = "SunshineSessionMonitorClass";
    wnd_class.lpfnWndProc = SessionMonitorWindowProc;
    if (!RegisterClassA(&wnd_class)) {
      session_monitor_hwnd_promise.set_value(nullptr);
      logging::error("Failed to register session monitor window class\n");
      return;
    }

    auto wnd = CreateWindowExA(
      0,
      wnd_class.lpszClassName,
      "Sunshine Session Monitor Window",
      0,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      nullptr,
      nullptr,
      nullptr,
      &lifetime
    );

    session_monitor_hwnd_promise.set_value(wnd);

    if (!wnd) {
      logging::error("Failed to create session monitor window\n");
      return;
    }

    ShowWindow(wnd, SW_HIDE);

    // Run the message loop for our window
    MSG msg {};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  });

  auto session_monitor_join_thread_guard = util::fail_guard([&session_monitor_hwnd_future, &session_monitor_thread]() {
    if (HWND session_monitor_hwnd = session_monitor_hwnd_future.get()) {
      PostMessage(session_monitor_hwnd, WM_CLOSE, 0, 0);
    }
    session_monitor_thread.join();
  });
#endif

  input::service_t inputs {settings, task_pool, host_mail, infrastructure.cursor()};
  proc::proc_t processes {settings, displays, inputs};
  processes.refresh(settings.stream.file_apps);

  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.

  platf::runtime_t platform {settings, host_mail, task_pool, lifetime, displays
#ifdef _WIN32
                             ,
                             nvprefs_instance
#endif
  };
  if (!platform.initialize()) {
    logging::error("Platform failed to initialize"sv);
  }

  // Capture the pre-request state so access granted during this launch causes
  // one clean restart after every verifiable required permission is available.
  const bool permission_restart_needed = !platf::required_permissions_granted(platf::get_permission_statuses(settings));
#ifdef __APPLE__
  platf::request_startup_permissions(settings, tray_is_enabled && settings.sunshine.system_tray);
#endif

  reed_solomon_init();
  inputs.initialize();

  if (settings.input.controller && inputs.probe_gamepads()) {
    logging::warning("No gamepad input is available"sv);
  }

  video::service_t videos {settings, host_mail, infrastructure.cursor(), displays, platform};
  if (videos.probe_encoders()) {
    logging::error("Video failed to find working encoder"sv);
  }

  std::unique_ptr<platf::deinit_t> mDNS;
  auto sync_mDNS = std::async(std::launch::async, [&mDNS, &settings]() {
    mDNS = platf::publish::start(settings);
  });

  std::unique_ptr<platf::deinit_t> upnp_unmap;
  auto sync_upnp = std::async(std::launch::async, [&upnp_unmap, &settings, host_mail]() {
    upnp_unmap = upnp::start(settings, host_mail);
  });

  // FIXME: Temporary workaround: Simple-Web_server needs to be updated or replaced
  if (shutdown_event->peek()) {
    return lifetime.exit_code();
  }

  stream::service_t streams {settings, host_mail, task_pool, inputs, videos, processes, displays, platform, audios};
  rtsp_stream::service_t rtsp {settings, streams, inputs, videos, host_mail};
  nvhttp::service_t gamestream {settings, http_state, processes, displays, videos, rtsp, host_mail};
  confighttp::service_t web {configuration, gamestream, http_state, processes, displays, inputs, rtsp, lifetime, host_mail};
  system_tray::application_binding_t tray_binding {settings, lifetime, displays};
  std::jthread httpThread {[&gamestream]() {
    gamestream.start();
  }};
  std::jthread configThread {[&web]() {
    web.start();
  }};
  std::jthread rtspThread {[&rtsp]() {
    rtsp.start();
  }};

#ifdef _WIN32
  // If we're using the default port and GameStream is enabled, warn the user
  if (settings.sunshine.port == 47989 && is_gamestream_enabled()) {
    logging::fatal("GameStream is still enabled in GeForce Experience! This *will* cause streaming problems with Sunshine!"sv);
    logging::fatal("Disable GameStream on the SHIELD tab in GeForce Experience or change the Port setting on the Advanced tab in the Sunshine Web UI."sv);
  }
#endif

  if (tray_is_enabled && settings.sunshine.system_tray) {
    logging::info("Starting system tray"sv);
#if defined(_WIN32) || defined(__APPLE__)
    system_tray::prepare_tray_virtualhid_license(settings);
    system_tray::prepare_tray_virtualhid_driver(settings);
#endif
#ifdef _WIN32
    // TODO: Windows has a weird bug where when running as a service and on the first Windows boot,
    // the tray icon would not appear even though Sunshine is running correctly otherwise.
    // Restarting the service would allow the icon to appear normally.
    // For now we will keep the Windows tray icon on a separate thread.
    // Ideally, we would run the system tray on the main thread for all platforms.
    system_tray::init_tray_threaded();
#else
    system_tray::init_tray();
#endif
  }

#ifdef __APPLE__
  std::jthread macos_audio_permission_requester;
  if (!permission_restart_needed) {
    macos_audio_permission_requester = std::jthread([&settings]() {
      platf::request_startup_system_audio_permission(settings);
    });
  }
#endif

  std::jthread permission_watcher;
  if (permission_restart_needed) {
    permission_watcher = std::jthread([&lifetime, &settings](std::stop_token stop) {
      while (!stop.stop_requested()) {
        if (platf::required_permissions_granted(platf::get_permission_statuses(settings))) {
          logging::info("Required permissions granted; restarting Sunshine"sv);
          platf::restart(lifetime);
          return;
        }
        std::this_thread::sleep_for(2s);
      }
    });
  }

  mainThreadLoop(settings, shutdown_event);

  permission_watcher.request_stop();
  if (permission_watcher.joinable()) {
    permission_watcher.join();
  }
#ifdef __APPLE__
  if (macos_audio_permission_requester.joinable()) {
    macos_audio_permission_requester.join();
  }
#endif

  httpThread.join();
  configThread.join();
  rtspThread.join();

  task_pool.stop();
  task_pool.join();

#ifdef _WIN32
  // Restore global NVIDIA control panel settings
  if (nvprefs_instance.owning_undo_file() && nvprefs_instance.load()) {
    nvprefs_instance.restore_global_profile();
    nvprefs_instance.unload();
  }
#endif

  return lifetime.exit_code();
}
