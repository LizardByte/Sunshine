/**
 * @file src/process.h
 * @brief Declarations for the startup and shutdown of the apps started by a streaming Session.
 */
#pragma once

#ifndef __kernel_entry
  #define __kernel_entry
#endif

// standard includes
#include <optional>
#include <unordered_map>

// lib includes
#include <boost/process/v1.hpp>

// local includes
#include "config.h"
#include "platform/common.h"
#include "rtsp.h"
#include "utility.h"

namespace proc {
  using file_t = util::safe_ptr_v2<FILE, int, fclose>;
  using cmd_t = config::prep_cmd_t;

  /**
   * pre_cmds -- guaranteed to be executed unless any of the commands fail.
   * detached -- commands detached from Sunshine
   * cmd -- Runs indefinitely until:
   *    No session is running and a different set of commands it to be executed
   *    Command exits
   * working_dir -- the process working directory. This is required for some games to run properly.
   * cmd_output --
   *    empty    -- The output of the commands are appended to the output of sunshine
   *    "null"   -- The output of the commands are discarded
   *    filename -- The output of the commands are appended to filename
   */
  struct ctx_t {
    std::vector<cmd_t> prep_cmds;

    /**
     * Some applications, such as Steam, either exit quickly, or keep running indefinitely.
     *
     * Apps that launch normal child processes and terminate will be handled by the process
     * grouping logic (wait_all). However, apps that launch child processes indirectly or
     * into another process group (such as UWP apps) can only be handled by the auto-detach
     * heuristic which catches processes that exit 0 very quickly, but we won't have proper
     * process tracking for those.
     *
     * For cases where users just want to kick off a background process and never manage the
     * lifetime of that process, they can use detached commands for that.
     */
    std::vector<std::string> detached;

    std::string name;
    std::string cmd;
    std::string working_dir;
    std::string output;
    std::string image_path;
    std::string id;
    bool elevated;
    bool auto_detach;
    bool wait_all;
    std::chrono::seconds exit_timeout;
  };

  /**
   * @brief App class that contains all of what is necessary for executing and terminating app.
   */
  class app_t {
  public:
    /**
     * @brief Start the app based on the provided context, env and session settings.
     * @param app_context Data required for starting the app.
     * @param app_id Requested app id for starting the app.
     * @param env Environment for the app processes.
     * @param launch_session Additional parameters provided during stream start/resume.
     * @returns An instance of the `app_t` class or a "return code" in case the app could not be started.
     */
    static std::variant<std::unique_ptr<app_t>, int> start(ctx_t app_context, int app_id, boost::process::v1::environment env, const rtsp_stream::launch_session_t &launch_session);

    /**
     * @brief Destructor that automatically cleans up after the app, terminating processes if needed.
     */
    virtual ~app_t();

    /**
     * @brief Get id of the app if it's still running.
     *
     * It is not necessary for any kind of process to be actually running, for the
     * app to be considered "running". App can be of a detached type - without any process.
     *
     * @note This method pools for the process state every time it is called and may perform early cleanup.
     * @return App id that was provided during start if the app is still running, empty optional otherwise
     */
    std::optional<int> get_app_id();

  private:
    /**
     * @brief Private constructor to enforce the usage of `start` method.
     */
    explicit app_t() = default;

    /**
     * @brief Execute the app based on the provided context, env and session settings.
     * @param app_context Data required for starting the app.
     * @param app_id Requested app id for starting the app.
     * @param env Environment for the app processes.
     * @param launch_session Additional parameters provided during stream start/resume.
     * @returns "Return code" - `0` on success and other values on failure.
     */
    int execute(ctx_t app_context, int app_id, boost::process::v1::environment env, const rtsp_stream::launch_session_t &launch_session);

    /**
     * @brief Terminate the currently running process (if any) and run undo commands.
     */
    void terminate();

    /**
     * @brief Terminate all child processes in a process group.
     * @param proc The child process itself.
     * @param group The group of all children in the process tree.
     * @param exit_timeout The timeout to wait for the process group to gracefully exit.
     */
    static void terminate_process_group(boost::process::v1::child &proc, boost::process::v1::group &group, std::chrono::seconds exit_timeout);

    int _app_id {0};  ///< A hash representing the app.
    bool _is_detached {false};  ///< App has no cmd, or it has successfully terminated withing 5s (configurable) as is now considered as detached.

    ctx_t _context;  ///< Parsed app context from settings.
    std::vector<cmd_t>::const_iterator _prep_cmd_it;  ///< Prep cmd iterator used as a starting point when terminating app if execution failed and not all undo commands need to be executed.
    std::chrono::steady_clock::time_point _launch_time;  ///< Time at which the app was executed.

    boost::process::v1::environment _env;  ///< Environment used during execution and termination.
    boost::process::v1::child _process;  ///< Process handle.
    boost::process::v1::group _process_group;  ///< Process group handle.
    file_t _output_pipe;  ///< Pipe to an optional output file.
  };

  /**
   * @brief Thread-safe wrapper-like class around the app list for handling the execution and termination of the app.
   */
  class proc_t {
  public:
    /**
     * @brief Destructor that may assert in debug mode if the app was not terminated at this point.
     */
    virtual ~proc_t();

    /**
     * @brief Execute the app based on the app id and session settings.
     * @param app_id Requested app id for starting the app.
     * @param launch_session Additional parameters provided during stream start/resume.
     * @returns "Return code" - `0` on success and other values on failure.
     * @note The apps list for `app_id` is populated by calling `refresh`.
     */
    int execute(int app_id, std::shared_ptr<rtsp_stream::launch_session_t> launch_session);

    /**
     * @brief Get id of the app if it's still running.
     * @return App id that was provided during execution if the app is still running, empty optional otherwise
     */
    std::optional<int> get_running_app_id();

    /**
     * @brief Terminate the currently running process.
     */
    void terminate();

    /**
     * @brief Get the list of applications.
     * @return A list of applications.
     */
    std::vector<ctx_t> get_apps() const;

    /**
     * @brief Get the image path of the application with the given app_id.
     *
     * - returns image from assets directory if found there.
     * - returns default image if image configuration is not set.
     * - returns http content-type header compatible image type.
     *
     * @param app_id The id of the application.
     * @return The image path of the application.
     */
    std::string get_app_image(int app_id) const;

    /**
     * @brief Get the name of the last run application (not necessarily successfully started).
     * @return The name of the last run application.
     */
    std::string get_last_run_app_name() const;

    /**
     * @brief Refresh the app list and env to be used for the next execution of the app.
     * @param file_name File to be parsed for the app list.
     */
    void refresh(const std::string &file_name);

  private:
    mutable std::recursive_mutex _mutex;  ///< Mutex to sync access from multiple threads.
    std::unique_ptr<app_t> _started_app;  ///< Successfully started app that may or may no longer be running.
    boost::process::v1::environment _env;  ///< Environment to be used for a new app execution.
    std::vector<ctx_t> _apps;  ///< App contexts for starting new apps.
    std::string _last_run_app_name;  ///< Name of the last executed application.
  };

  /// The legendary global of the proc_t
  extern proc_t proc;

  /**
   * @brief Initialize proc functions.
   * @return Unique pointer to `deinit_t` to manage cleanup.
   */
  std::unique_ptr<platf::deinit_t> init();

  /**
   * @brief Get the working directory for the file path.
   * @param file_path File path to be parsed.
   * @return Working directory.
   */
  boost::filesystem::path find_working_directory(const std::string &file_path);

  /**
   * @brief Parse the string and replace any "$(...)" patterns with a value from env.
   * @param env Environment to be used as a source for replacement.
   * @param val_raw Raw string to be parsed.
   * @returns Strings with replacements made (if any) with values from env.
   * @warning This function throws if the `val_raw` is ill-formed.
   */
  std::string parse_env_val(const boost::process::v1::native_environment &env, std::string_view val_raw);

  /**
   * @brief Validate the image path.
   * @param app_image_path File path to validate.
   *
   * Requirements:
   *  - images must be of `.png` file ending
   *  - image file must exist (can be relative to the `assets` directory).
   *
   * @returns Validated image path on success, default image path on failure.
   */
  std::string validate_app_image_path(const std::string &app_image_path);

  /**
   * @brief Calculate SHA256 from the file contents.
   * @param file_name File to be used for calculation.
   * @returns Calculated hash string or empty optional if hash could not be calculated.
   */
  std::optional<std::string> calculate_sha256(const std::string &file_name);

  /**
   * @brief Calculate CRC32 for the input string.
   * @param input String to calculate hash for.
   * @returns Hash for the input string.
   */
  uint32_t calculate_crc32(const std::string &input);

  /**
   * @brief Calculate a stable id based on name and image data (and an app index if required).
   * @param app_name App name.
   * @param app_image_path App image path.
   * @param app_index App index in the app list.
   * @return Tuple of id calculated without index (for use if no collision) and one with.
   */
  std::tuple<std::string, std::string> calculate_app_id(const std::string &app_name, const std::string &app_image_path, int app_index);

  /**
   * @brief Parse the app list file.
   * @param file_name The app list file location.
   * @returns A tuple of parsed apps list and the environment used for parsing, empty optional if parsing has failed.
   */
  std::optional<std::tuple<boost::process::v1::environment, std::vector<ctx_t>>> parse(const std::string &file_name);
}  // namespace proc
