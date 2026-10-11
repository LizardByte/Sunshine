/**
 * @file entry_handler.h
 * @brief Declarations for entry handling functions.
 */
#pragma once
#include "config.h"

// standard includes
#include <atomic>
#include <csignal>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// local includes
#include "thread_pool.h"
#include "thread_safe.h"

/**
 * @brief Build the URL used to launch the Web UI.
 * @param settings Application settings containing the bind address and base port.
 * @param path Optional path to append to the base URL.
 * @return HTTPS URL using the configured bind address and Web UI port.
 * @examples
 * get_launch_ui_url(settings);
 * get_launch_ui_url(settings, "/pin");
 * @examples_end
 */
std::string get_launch_ui_url(const config::settings_t &settings, const std::optional<std::string> &path = std::nullopt);

/**
 * @brief Launch the Web UI.
 * @param settings Application settings containing the bind address and base port.
 * @param path Optional path to append to the base URL.
 */
void launch_ui(const config::settings_t &settings, const std::optional<std::string> &path = std::nullopt);

/**
 * @brief Functions for handling command line arguments.
 */
namespace args {
  /**
   * @brief Reset the user credentials.
   * @param settings Application settings containing the credentials file path.
   * @param name The name of the program.
   * @param argc The number of arguments.
   * @param argv The arguments.
   * @examples
   * creds(settings, "sunshine", 2, {"new_username", "new_password"});
   * @examples_end
   *
   * @return Process exit code from updating the stored credentials.
   */
  int creds(const config::settings_t &settings, const char *name, int argc, char *argv[]);

  /**
   * @brief Print help to stdout, then exit.
   * @param name The name of the program.
   * @examples
   * help("sunshine");
   * @examples_end
   *
   * @return Process exit code after printing command usage.
   */
  int help(const char *name);

  /**
   * @brief Print the version to stdout, then exit.
   * @examples
   * version();
   * @examples_end
   *
   * @return Process exit code after printing the Sunshine version.
   */
  int version();

#ifdef _WIN32
  /**
   * @brief Restore global NVIDIA control panel settings.
   * If Sunshine was improperly terminated, this function restores
   * the global NVIDIA control panel settings to the undo file left
   * by Sunshine. This function is typically called by the uninstaller.
   * @examples
   * restore_nvprefs_undo();
   * @examples_end
   *
   * @return Process exit code from restoring NVIDIA profile preferences.
   */
  int restore_nvprefs_undo();
#endif
}  // namespace args

/**
 * @brief Functions for handling the lifetime of Sunshine.
 */
namespace lifetime {
  /**
   * @brief Owns command arguments, exit status, and an optional process restart action.
   */
  class controller_t {
  public:
    /**
     * @brief Capture the original command arguments for this application lifetime.
     * @param argv Original argument vector; valid for the controller lifetime.
     */
    explicit controller_t(char **argv = nullptr);
    controller_t(const controller_t &) = delete;
    controller_t &operator=(const controller_t &) = delete;

    /**
     * @brief Run a requested restart after the application's resources are released.
     */
    ~controller_t();

    /**
     * @brief Request graceful shutdown and preserve the first nonzero exit status.
     * @param exit_code Status returned by main().
     * @param async Whether the caller can return before shutdown completes.
     */
    void exit_sunshine(int exit_code, bool async);

    /**
     * @brief Read the status requested by application shutdown.
     * @return Requested process exit status.
     */
    int exit_code() const;

    /**
     * @brief Read the original argument vector.
     * @return Arguments captured at construction.
     */
    char **get_argv() const;

    /**
     * @brief Arrange a restart after this controller's dependents are destroyed.
     * @param action Process relaunch operation.
     */
    void restart_on_exit(std::function<void()> action);

  private:
    char **argv_;  ///< Original argument vector.
    std::atomic_int desired_exit_code_ {};  ///< First nonzero requested exit status.
    std::function<void()> restart_action_;  ///< Relaunch operation owned by this lifetime.
  };

  /**
   * @brief Owns signal callbacks and restores native handlers at the end of their scope.
   *
   * POSIX signals and Windows console callbacks cannot carry userdata. Only these
   * native trampolines use the scoped active binding; application code receives
   * its controller explicitly.
   */
  class signal_guard_t {
  public:
    /**
     * @brief Bind native callbacks to one application lifetime.
     * @param controller Application shutdown controller.
     */
    explicit signal_guard_t(controller_t &controller);
    signal_guard_t(const signal_guard_t &) = delete;
    signal_guard_t &operator=(const signal_guard_t &) = delete;
    /**
     * @brief Restore the handlers replaced by this guard.
     */
    ~signal_guard_t();

    /**
     * @brief Install an application callback for one POSIX signal.
     * @param signal Application termination signal, SIGINT or SIGTERM.
     * @param handler Callback invoked by the native trampoline.
     */
    void on_signal(int signal, std::function<void()> handler);

    /**
     * @brief Request shutdown from a native callback without a userdata argument.
     * @param exit_code Requested process status.
     * @param async Whether the callback can return before shutdown completes.
     */
    static void native_exit(int exit_code, bool async);

  private:
    /**
     * @brief Obtain the binding used exclusively by native callback trampolines.
     * @return Atomic pointer to the guard registered in the current scope.
     */
    static std::atomic<signal_guard_t *> &native_binding();
    /**
     * @brief Reinstall a native signal handler before forwarding to its owned callback.
     * @param signal Signal delivered by the operating system.
     */
    static void forward(int signal);

    controller_t &controller_;  ///< Application lifetime receiving console shutdown.
    std::map<int, std::function<void()>> handlers_;  ///< Callbacks owned by this registration.
    std::map<int, void (*)(int)> previous_handlers_;  ///< Native handlers restored on destruction.
  };

  /**
   * @brief Breaks into the debugger or terminates Sunshine if no debugger is attached.
   */
  void debug_trap();

}  // namespace lifetime

/**
 * @brief Log the publisher metadata provided from CMake.
 */
void log_publisher_data();

#ifdef _WIN32
/**
 * @brief Check if NVIDIA's GameStream software is running.
 * @return `true` if GameStream is enabled, `false` otherwise.
 */
bool is_gamestream_enabled();

/**
 * @brief Namespace for controlling the Sunshine service model on Windows.
 */
namespace service_ctrl {
  /**
   * @brief Check if the service is running.
   * @examples
   * is_service_running();
   * @examples_end
   *
   * @return True when the requested service or UI readiness condition is satisfied.
   */
  bool is_service_running();

  /**
   * @brief Start the service and wait for startup to complete.
   * @examples
   * start_service();
   * @examples_end
   *
   * @return True when the requested service or UI readiness condition is satisfied.
   */
  bool start_service();

  /**
   * @brief Wait for the UI to be ready after Sunshine startup.
   * @param settings Application settings containing the Web UI endpoint.
   * @examples
   * wait_for_ui_ready(settings);
   * @examples_end
   *
   * @return True when the requested service or UI readiness condition is satisfied.
   */
  bool wait_for_ui_ready(const config::settings_t &settings);
}  // namespace service_ctrl
#endif
