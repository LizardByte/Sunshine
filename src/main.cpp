/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

// lib includes
#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>

// local includes
#include "config.h"
#include "confighttp.h"
#include "httpcommon.h"
#include "main.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"
#include "rtsp.h"
#include "system_tray.h"
#include "thread_pool.h"
#include "upnp.h"
#include "version.h"
#include "video.h"

extern "C" {
#include <libavutil/log.h>
#include <rs.h>

#ifdef _WIN32
  #include <iphlpapi.h>
#endif
}

safe::mail_t mail::man;

using namespace std::literals;
namespace bl = boost::log;

#ifdef _WIN32
// Define global singleton used for NVIDIA control panel modifications
nvprefs::nvprefs_interface nvprefs_instance;
#endif

thread_pool_util::ThreadPool task_pool;
bl::sources::severity_logger<int> verbose(0);  // Dominating output
bl::sources::severity_logger<int> debug(1);  // Follow what is happening
bl::sources::severity_logger<int> info(2);  // Should be informed about
bl::sources::severity_logger<int> warning(3);  // Strange events
bl::sources::severity_logger<int> error(4);  // Recoverable errors
bl::sources::severity_logger<int> fatal(5);  // Unrecoverable errors

bool display_cursor = true;

using text_sink = bl::sinks::asynchronous_sink<bl::sinks::text_ostream_backend>;
boost::shared_ptr<text_sink> sink;

struct NoDelete {
  void
  operator()(void *) {}
};

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)

/**
 * @brief Print help to stdout.
 * @param name The name of the program.
 *
 * EXAMPLES:
 * ```cpp
 * print_help("sunshine");
 * ```
 */
void
print_help(const char *name) {
  std::cout
    << "Usage: "sv << name << " [options] [/path/to/configuration_file] [--cmd]"sv << std::endl
    << "    Any configurable option can be overwritten with: \"name=value\""sv << std::endl
    << std::endl
    << "    Note: The configuration will be created if it doesn't exist."sv << std::endl
    << std::endl
    << "    --help                    | print help"sv << std::endl
    << "    --creds username password | set user credentials for the Web manager"sv << std::endl
    << "    --version                 | print the version of sunshine"sv << std::endl
    << std::endl
    << "    flags"sv << std::endl
    << "        -0 | Read PIN from stdin"sv << std::endl
    << "        -1 | Do not load previously saved state and do retain any state after shutdown"sv << std::endl
    << "           | Effectively starting as if for the first time without overwriting any pairings with your devices"sv << std::endl
    << "        -2 | Force replacement of headers in video stream"sv << std::endl
    << "        -p | Enable/Disable UPnP"sv << std::endl
    << std::endl;
}

namespace help {
  int
  entry(const char *name, int argc, char *argv[]) {
    print_help(name);
    return 0;
  }
}  // namespace help

namespace version {
  int
  entry(const char *name, int argc, char *argv[]) {
    std::cout << PROJECT_NAME << " version: v" << PROJECT_VER << std::endl;
    return 0;
  }
}  // namespace version

#ifdef _WIN32
namespace restore_nvprefs_undo {
  int
  entry(const char *name, int argc, char *argv[]) {
    // Restore global NVIDIA control panel settings to the undo file
    // left by improper termination of sunshine.exe, if it exists.
    // This entry point is typically called by the uninstaller.
    if (nvprefs_instance.load()) {
      nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
      nvprefs_instance.unload();
    }
    return 0;
  }
}  // namespace restore_nvprefs_undo
#endif

namespace lifetime {
  static char **argv;
  static std::atomic_int desired_exit_code;

  /**
   * @brief Terminates Sunshine gracefully with the provided exit code.
   * @param exit_code The exit code to return from main().
   * @param async Specifies whether our termination will be non-blocking.
   */
  void
  exit_sunshine(int exit_code, bool async) {
    // Store the exit code of the first exit_sunshine() call
    int zero = 0;
    desired_exit_code.compare_exchange_strong(zero, exit_code);

    // Raise SIGINT to start termination
    std::raise(SIGINT);

    // Termination will happen asynchronously, but the caller may
    // have wanted synchronous behavior.
    while (!async) {
      std::this_thread::sleep_for(1s);
    }
  }

  /**
   * @brief Gets the argv array passed to main().
   */
  char **
  get_argv() {
    return argv;
  }
}  // namespace lifetime

#ifdef _WIN32
namespace service_ctrl {
  class service_controller {
  public:
    /**
     * @brief Constructor for service_controller class.
     * @param service_desired_access SERVICE_* desired access flags.
     */
    service_controller(DWORD service_desired_access) {
      scm_handle = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
      if (!scm_handle) {
        auto winerr = GetLastError();
        BOOST_LOG(error) << "OpenSCManager() failed: "sv << winerr;
        return;
      }

      service_handle = OpenServiceA(scm_handle, "SunshineService", service_desired_access);
      if (!service_handle) {
        auto winerr = GetLastError();
        BOOST_LOG(error) << "OpenService() failed: "sv << winerr;
        return;
      }
    }

    ~service_controller() {
      if (service_handle) {
        CloseServiceHandle(service_handle);
      }

      if (scm_handle) {
        CloseServiceHandle(scm_handle);
      }
    }

    /**
     * @brief Asynchronously starts the Sunshine service.
     */
    bool
    start_service() {
      if (!service_handle) {
        return false;
      }

      if (!StartServiceA(service_handle, 0, nullptr)) {
        auto winerr = GetLastError();
        if (winerr != ERROR_SERVICE_ALREADY_RUNNING) {
          BOOST_LOG(error) << "StartService() failed: "sv << winerr;
          return false;
        }
      }

      return true;
    }

    /**
     * @brief Query the service status.
     * @param status The SERVICE_STATUS struct to populate.
     */
    bool
    query_service_status(SERVICE_STATUS &status) {
      if (!service_handle) {
        return false;
      }

      if (!QueryServiceStatus(service_handle, &status)) {
        auto winerr = GetLastError();
        BOOST_LOG(error) << "QueryServiceStatus() failed: "sv << winerr;
        return false;
      }

      return true;
    }

  private:
    SC_HANDLE scm_handle = NULL;
    SC_HANDLE service_handle = NULL;
  };

  /**
   * @brief Check if the service is running.
   *
   * EXAMPLES:
   * ```cpp
   * is_service_running();
   * ```
   */
  bool
  is_service_running() {
    service_controller sc { SERVICE_QUERY_STATUS };

    SERVICE_STATUS status;
    if (!sc.query_service_status(status)) {
      return false;
    }

    return status.dwCurrentState == SERVICE_RUNNING;
  }

  /**
   * @brief Start the service and wait for startup to complete.
   *
   * EXAMPLES:
   * ```cpp
   * start_service();
   * ```
   */
  bool
  start_service() {
    service_controller sc { SERVICE_QUERY_STATUS | SERVICE_START };

    std::cout << "Starting Sunshine..."sv;

    // This operation is asynchronous, so we must wait for it to complete
    if (!sc.start_service()) {
      return false;
    }

    SERVICE_STATUS status;
    do {
      Sleep(1000);
      std::cout << '.';
    } while (sc.query_service_status(status) && status.dwCurrentState == SERVICE_START_PENDING);

    if (status.dwCurrentState != SERVICE_RUNNING) {
      BOOST_LOG(error) << SERVICE_NAME " failed to start: "sv << status.dwWin32ExitCode;
      return false;
    }

    std::cout << std::endl;
    return true;
  }

  /**
   * @brief Wait for the UI to be ready after Sunshine startup.
   *
   * EXAMPLES:
   * ```cpp
   * wait_for_ui_ready();
   * ```
   */
  bool
  wait_for_ui_ready() {
    std::cout << "Waiting for Web UI to be ready...";

    // Wait up to 30 seconds for the web UI to start
    for (int i = 0; i < 30; i++) {
      PMIB_TCPTABLE tcp_table = nullptr;
      ULONG table_size = 0;
      ULONG err;

      auto fg = util::fail_guard([&tcp_table]() {
        free(tcp_table);
      });

      do {
        // Query all open TCP sockets to look for our web UI port
        err = GetTcpTable(tcp_table, &table_size, false);
        if (err == ERROR_INSUFFICIENT_BUFFER) {
          free(tcp_table);
          tcp_table = (PMIB_TCPTABLE) malloc(table_size);
        }
      } while (err == ERROR_INSUFFICIENT_BUFFER);

      if (err != NO_ERROR) {
        BOOST_LOG(error) << "Failed to query TCP table: "sv << err;
        return false;
      }

      uint16_t port_nbo = htons(map_port(confighttp::PORT_HTTPS));
      for (DWORD i = 0; i < tcp_table->dwNumEntries; i++) {
        auto &entry = tcp_table->table[i];

        // Look for our port in the listening state
        if (entry.dwLocalPort == port_nbo && entry.dwState == MIB_TCP_STATE_LISTEN) {
          std::cout << std::endl;
          return true;
        }
      }

      Sleep(1000);
      std::cout << '.';
    }

    std::cout << "timed out"sv << std::endl;
    return false;
  }
}  // namespace service_ctrl

/**
 * @brief Checks if NVIDIA's GameStream software is running.
 * @return `true` if GameStream is enabled.
 */
bool
is_gamestream_enabled() {
  DWORD enabled;
  DWORD size = sizeof(enabled);
  return RegGetValueW(
           HKEY_LOCAL_MACHINE,
           L"SOFTWARE\\NVIDIA Corporation\\NvStream",
           L"EnableStreaming",
           RRF_RT_REG_DWORD,
           nullptr,
           &enabled,
           &size) == ERROR_SUCCESS &&
         enabled != 0;
}

#endif

/**
 * @brief Launch the Web UI.
 *
 * EXAMPLES:
 * ```cpp
 * launch_ui();
 * ```
 */
void
launch_ui() {
  std::string url = "https://localhost:" + std::to_string(map_port(confighttp::PORT_HTTPS));
  platf::open_url(url);
}

/**
 * @brief Launch the Web UI at a specific endpoint.
 *
 * EXAMPLES:
 * ```cpp
 * launch_ui_with_path("/pin");
 * ```
 */
void
launch_ui_with_path(std::string path) {
  std::string url = "https://localhost:" + std::to_string(map_port(confighttp::PORT_HTTPS)) + path;
  platf::open_url(url);
}

/**
 * @brief Flush the log.
 *
 * EXAMPLES:
 * ```cpp
 * log_flush();
 * ```
 */
void
log_flush() {
  sink->flush();
}

std::map<int, std::function<void()>> signal_handlers;
void
on_signal_forwarder(int sig) {
  signal_handlers.at(sig)();
}

template <class FN>
void
on_signal(int sig, FN &&fn) {
  signal_handlers.emplace(sig, std::forward<FN>(fn));

  std::signal(sig, on_signal_forwarder);
}

namespace gen_creds {
  int
  entry(const char *name, int argc, char *argv[]) {
    if (argc < 2 || argv[0] == "help"sv || argv[1] == "help"sv) {
      print_help(name);
      return 0;
    }

    http::save_user_creds(config::sunshine.credentials_file, argv[0], argv[1]);

    return 0;
  }
}  // namespace gen_creds

std::map<std::string_view, std::function<int(const char *name, int argc, char **argv)>> cmd_to_func {
  { "creds"sv, gen_creds::entry },
  { "help"sv, help::entry },
  { "version"sv, version::entry },
#ifdef _WIN32
  { "restore-nvprefs-undo"sv, restore_nvprefs_undo::entry },
#endif
};

#ifdef _WIN32
LRESULT CALLBACK
SessionMonitorWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_ENDSESSION: {
      // Terminate ourselves with a blocking exit call
      std::cout << "Received WM_ENDSESSION"sv << std::endl;
      lifetime::exit_sunshine(0, false);
      return 0;
    }
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}
#endif

/**
 * @brief Main application entry point.
 * @param argc The number of arguments.
 * @param argv The arguments.
 *
 * EXAMPLES:
 * ```cpp
 * main(1, const char* args[] = {"sunshine", nullptr});
 * ```
 */
int
main(int argc, char *argv[]) {
  lifetime::argv = argv;

  task_pool_util::TaskPool::task_id_t force_shutdown = nullptr;

#ifdef _WIN32
  // Switch default C standard library locale to UTF-8 on Windows 10 1803+
  setlocale(LC_ALL, ".UTF-8");
#endif

  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale::global(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

  mail::man = std::make_shared<safe::mail_raw_t>();

  if (config::parse(argc, argv)) {
    return 0;
  }

  if (config::sunshine.min_log_level >= 1) {
    av_log_set_level(AV_LOG_QUIET);
  }
  else {
    av_log_set_level(AV_LOG_DEBUG);
  }
  av_log_set_callback([](void *ptr, int level, const char *fmt, va_list vl) {
    static int print_prefix = 1;
    char buffer[1024];

    av_log_format_line(ptr, level, fmt, vl, buffer, sizeof(buffer), &print_prefix);
    if (level <= AV_LOG_ERROR) {
      // We print AV_LOG_FATAL at the error level. FFmpeg prints things as fatal that
      // are expected in some cases, such as lack of codec support or similar things.
      BOOST_LOG(error) << buffer;
    }
    else if (level <= AV_LOG_WARNING) {
      BOOST_LOG(warning) << buffer;
    }
    else if (level <= AV_LOG_INFO) {
      BOOST_LOG(info) << buffer;
    }
    else if (level <= AV_LOG_VERBOSE) {
      // AV_LOG_VERBOSE is less verbose than AV_LOG_DEBUG
      BOOST_LOG(debug) << buffer;
    }
    else {
      BOOST_LOG(verbose) << buffer;
    }
  });

  sink = boost::make_shared<text_sink>();

  boost::shared_ptr<std::ostream> stream { &std::cout, NoDelete {} };
  sink->locked_backend()->add_stream(stream);
  sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(config::sunshine.log_file));
  sink->set_filter(severity >= config::sunshine.min_log_level);

  sink->set_formatter([message = "Message"s, severity = "Severity"s](const bl::record_view &view, bl::formatting_ostream &os) {
    constexpr int DATE_BUFFER_SIZE = 21 + 2 + 1;  // Full string plus ": \0"

    auto log_level = view.attribute_values()[severity].extract<int>().get();

    std::string_view log_type;
    switch (log_level) {
      case 0:
        log_type = "Verbose: "sv;
        break;
      case 1:
        log_type = "Debug: "sv;
        break;
      case 2:
        log_type = "Info: "sv;
        break;
      case 3:
        log_type = "Warning: "sv;
        break;
      case 4:
        log_type = "Error: "sv;
        break;
      case 5:
        log_type = "Fatal: "sv;
        break;
    };

    char _date[DATE_BUFFER_SIZE];
    std::time_t t = std::time(nullptr);
    strftime(_date, DATE_BUFFER_SIZE, "[%Y:%m:%d:%H:%M:%S]: ", std::localtime(&t));

    os << _date << log_type << view.attribute_values()[message].extract<std::string>();
  });

  // Flush after each log record to ensure log file contents on disk isn't stale.
  // This is particularly important when running from a Windows service.
  sink->locked_backend()->auto_flush(true);

  bl::core::get()->add_sink(sink);
  auto fg = util::fail_guard(log_flush);

  if (!config::sunshine.cmd.name.empty()) {
    auto fn = cmd_to_func.find(config::sunshine.cmd.name);
    if (fn == std::end(cmd_to_func)) {
      BOOST_LOG(fatal) << "Unknown command: "sv << config::sunshine.cmd.name;

      BOOST_LOG(info) << "Possible commands:"sv;
      for (auto &[key, _] : cmd_to_func) {
        BOOST_LOG(info) << '\t' << key;
      }

      return 7;
    }

    return fn->second(argv[0], config::sunshine.cmd.argc, config::sunshine.cmd.argv);
  }

#ifdef WIN32
  // Modify relevant NVIDIA control panel settings if the system has corresponding gpu
  if (nvprefs_instance.load()) {
    // Restore global settings to the undo file left by improper termination of sunshine.exe
    nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
    // Modify application settings for sunshine.exe
    nvprefs_instance.modify_application_profile();
    // Modify global settings, undo file is produced in the process to restore after improper termination
    nvprefs_instance.modify_global_profile();
    // Unload dynamic library to survive driver reinstallation
    nvprefs_instance.unload();
  }

  // Wait as long as possible to terminate Sunshine.exe during logoff/shutdown
  SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

  // We must create a hidden window to receive shutdown notifications since we load gdi32.dll
  std::promise<HWND> session_monitor_hwnd_promise;
  auto session_monitor_hwnd_future = session_monitor_hwnd_promise.get_future();
  std::promise<void> session_monitor_join_thread_promise;
  auto session_monitor_join_thread_future = session_monitor_join_thread_promise.get_future();

  std::thread session_monitor_thread([&]() {
    session_monitor_join_thread_promise.set_value_at_thread_exit();

    WNDCLASSA wnd_class {};
    wnd_class.lpszClassName = "SunshineSessionMonitorClass";
    wnd_class.lpfnWndProc = SessionMonitorWindowProc;
    if (!RegisterClassA(&wnd_class)) {
      session_monitor_hwnd_promise.set_value(NULL);
      BOOST_LOG(error) << "Failed to register session monitor window class"sv << std::endl;
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
      nullptr);

    session_monitor_hwnd_promise.set_value(wnd);

    if (!wnd) {
      BOOST_LOG(error) << "Failed to create session monitor window"sv << std::endl;
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

  auto session_monitor_join_thread_guard = util::fail_guard([&]() {
    if (session_monitor_hwnd_future.wait_for(1s) == std::future_status::ready) {
      if (HWND session_monitor_hwnd = session_monitor_hwnd_future.get()) {
        PostMessage(session_monitor_hwnd, WM_CLOSE, 0, 0);
      }

      if (session_monitor_join_thread_future.wait_for(1s) == std::future_status::ready) {
        session_monitor_thread.join();
        return;
      }
      else {
        BOOST_LOG(warning) << "session_monitor_join_thread_future reached timeout";
      }
    }
    else {
      BOOST_LOG(warning) << "session_monitor_hwnd_future reached timeout";
    }

    session_monitor_thread.detach();
  });

#endif

  BOOST_LOG(info) << PROJECT_NAME << " version: " << PROJECT_VER << std::endl;
  task_pool.start(1);

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
  // create tray thread and detach it
  system_tray::run_tray();
#endif

  // Create signal handler after logging has been initialized
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [&force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      log_flush();
      std::abort();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_event->raise(true);
  });

  on_signal(SIGTERM, [&force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Terminate handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      log_flush();
      std::abort();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_event->raise(true);
  });

  proc::refresh(config::stream.file_apps);

  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.

  auto deinit_guard = platf::init();
  if (!deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
  }

  auto proc_deinit_guard = proc::init();
  if (!proc_deinit_guard) {
    BOOST_LOG(error) << "Proc failed to initialize"sv;
  }

  reed_solomon_init();
  auto input_deinit_guard = input::init();
  if (video::probe_encoders()) {
    BOOST_LOG(error) << "Video failed to find working encoder"sv;
  }

  if (http::init()) {
    BOOST_LOG(fatal) << "HTTP interface failed to initialize"sv;

#ifdef _WIN32
    BOOST_LOG(fatal) << "To relaunch Sunshine successfully, use the shortcut in the Start Menu. Do not run Sunshine.exe manually."sv;
    std::this_thread::sleep_for(10s);
#endif

    return -1;
  }

  std::unique_ptr<platf::deinit_t> mDNS;
  auto sync_mDNS = std::async(std::launch::async, [&mDNS]() {
    mDNS = platf::publish::start();
  });

  std::unique_ptr<platf::deinit_t> upnp_unmap;
  auto sync_upnp = std::async(std::launch::async, [&upnp_unmap]() {
    upnp_unmap = upnp::start();
  });

  // FIXME: Temporary workaround: Simple-Web_server needs to be updated or replaced
  if (shutdown_event->peek()) {
    return lifetime::desired_exit_code;
  }

  std::thread httpThread { nvhttp::start };
  std::thread configThread { confighttp::start };

#ifdef _WIN32
  // If we're using the default port and GameStream is enabled, warn the user
  if (config::sunshine.port == 47989 && is_gamestream_enabled()) {
    BOOST_LOG(fatal) << "GameStream is still enabled in GeForce Experience! This *will* cause streaming problems with Sunshine!"sv;
    BOOST_LOG(fatal) << "Disable GameStream on the SHIELD tab in GeForce Experience or change the Port setting on the Advanced tab in the Sunshine Web UI."sv;
  }
#endif

  rtsp_stream::rtpThread();

  httpThread.join();
  configThread.join();

  task_pool.stop();
  task_pool.join();

  // stop system tray
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
  system_tray::end_tray();
#endif

#ifdef WIN32
  // Restore global NVIDIA control panel settings
  if (nvprefs_instance.owning_undo_file() && nvprefs_instance.load()) {
    nvprefs_instance.restore_global_profile();
    nvprefs_instance.unload();
  }
#endif

  return lifetime::desired_exit_code;
}

/**
 * @brief Read a file to string.
 * @param path The path of the file.
 * @return `std::string` : The contents of the file.
 *
 * EXAMPLES:
 * ```cpp
 * std::string contents = read_file("path/to/file");
 * ```
 */
std::string
read_file(const char *path) {
  if (!std::filesystem::exists(path)) {
    BOOST_LOG(debug) << "Missing file: " << path;
    return {};
  }

  std::ifstream in(path);

  std::string input;
  std::string base64_cert;

  while (!in.eof()) {
    std::getline(in, input);
    base64_cert += input + '\n';
  }

  return base64_cert;
}

/**
 * @brief Writes a file.
 * @param path The path of the file.
 * @param contents The contents to write.
 * @return `int` : `0` on success, `-1` on failure.
 *
 * EXAMPLES:
 * ```cpp
 * int write_status = write_file("path/to/file", "file contents");
 * ```
 */
int
write_file(const char *path, const std::string_view &contents) {
  std::ofstream out(path);

  if (!out.is_open()) {
    return -1;
  }

  out << contents;

  return 0;
}

/**
 * @brief Map a specified port based on the base port.
 * @param port The port to map as a difference from the base port.
 * @return `std:uint16_t` : The mapped port number.
 *
 * EXAMPLES:
 * ```cpp
 * std::uint16_t mapped_port = map_port(1);
 * ```
 */
std::uint16_t
map_port(int port) {
  // calculate the port from the config port
  auto mapped_port = (std::uint16_t)((int) config::sunshine.port + port);

  // Ensure port is in the range of 1024-65535
  if (mapped_port < 1024 || mapped_port > 65535) {
    BOOST_LOG(warning) << "Port out of range: "sv << mapped_port;
  }

  // TODO: Ensure port is not already in use by another application

  return mapped_port;
}
