/**
 * @file entry_handler.cpp
 * @brief Entry point related functions.
 */

// standard includes
#include <csignal>
#include <iostream>
#include <thread>

// local includes
#include "config.h"
#include "confighttp.h"
#include "entry_handler.h"
#include "globals.h"
#include "httpcommon.h"
#include "logging.h"
#include "network.h"
#include "platform/common.h"
#include "version.h"

extern "C" {
#ifdef _WIN32
  #include <iphlpapi.h>
#endif
}

using namespace std::literals;

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
  std::string url = "https://localhost:" + std::to_string(net::map_port(confighttp::PORT_HTTPS));
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
  std::string url = "https://localhost:" + std::to_string(net::map_port(confighttp::PORT_HTTPS)) + path;
  platf::open_url(url);
}

namespace args {
  /**
   * @brief Reset the user credentials.
   *
   * @param name The name of the program.
   * @param argc The number of arguments.
   * @param argv The arguments.
   *
   * EXAMPLES:
   * ```cpp
   * creds("sunshine", 2, {"new_username", "new_password"});
   * ```
   */
  int
  creds(const char *name, int argc, char *argv[]) {
    if (argc < 2 || argv[0] == "help"sv || argv[1] == "help"sv) {
      help(name, argc, argv);
    }

    http::save_user_creds(config::sunshine.credentials_file, argv[0], argv[1]);

    return 0;
  }

  /**
   * @brief Print help to stdout, then exit.
   * @param name The name of the program.
   * @param argc The number of arguments. (Unused)
   * @param argv The arguments. (Unused)
   *
   * EXAMPLES:
   * ```cpp
   * print_help("sunshine", 0, nullptr);
   * ```
   */
  int
  help(const char *name, int argc, char *argv[]) {
    print_help(name);
    return 0;
  }

  /**
   * @brief Print the version to stdout, then exit.
   * @param name The name of the program. (Unused)
   * @param argc The number of arguments. (Unused)
   * @param argv The arguments. (Unused)
   *
   * EXAMPLES:
   * ```cpp
   * version("sunshine", 0, nullptr);
   * ```
   */
  int
  version(const char *name, int argc, char *argv[]) {
    std::cout << PROJECT_NAME << " version: v" << PROJECT_VER << std::endl;
    return 0;
  }

#ifdef _WIN32
  /**
   * @brief Restore global NVIDIA control panel settings.
   *
   * If Sunshine was improperly terminated, this function restores
   * the global NVIDIA control panel settings to the undo file left
   * by Sunshine. This function is typically called by the uninstaller.
   *
   * @param name The name of the program. (Unused)
   * @param argc The number of arguments. (Unused)
   * @param argv The arguments. (Unused)
   *
   * EXAMPLES:
   * ```cpp
   * restore_nvprefs_undo("sunshine", 0, nullptr);
   * ```
   */
  int
  restore_nvprefs_undo(const char *name, int argc, char *argv[]) {
    if (nvprefs_instance.load()) {
      nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
      nvprefs_instance.unload();
    }
    return 0;
  }
#endif
}  // namespace args

namespace lifetime {
  char **argv;
  std::atomic_int desired_exit_code;

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
   * @brief Breaks into the debugger or terminates Sunshine if no debugger is attached.
   */
  void
  debug_trap() {
#ifdef _WIN32
    DebugBreak();
#else
    std::raise(SIGTRAP);
#endif
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
/**
 * @brief Check if NVIDIA's GameStream software is running.
 * @return `true` if GameStream is enabled, `false` otherwise.
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

      uint16_t port_nbo = htons(net::map_port(confighttp::PORT_HTTPS));
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
#endif
