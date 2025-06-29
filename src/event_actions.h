/**
 * @file src/event_actions.h
 * @brief Declarations for the comprehensive event-action system.
 */
#pragma once

// standard includes
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace event_actions {

  /**
   * @brief Enum representing all possible event-action execution stages.
   */
  // Application/Stream start stages
  enum class stage_e {
    PRE_STREAM_START,       ///< Before the stream begins
    POST_STREAM_START,      ///< After the stream has started successfully
    PRE_DISPLAY_CHECK,      ///< Before display validation
    POST_DISPLAY_CHECK,     ///< After display has been validated
    ADDITIONAL_CLIENT,      ///< When an additional client connects
    STREAM_RESUME,          ///< When stream resumes from pause

    // Application/Stream cleanup stages  
    STREAM_PAUSE,           ///< When stream is paused
    PRE_STREAM_STOP,        ///< Before the stream stops
    PRE_DISPLAY_CLEANUP,    ///< Before display cleanup
    POST_DISPLAY_CLEANUP,   ///< After display cleanup
    POST_STREAM_STOP,       ///< After the stream has stopped
    ADDITIONAL_CLIENT_DISCONNECT  ///< When an additional client disconnects
  };

  /**
   * @brief Enum representing failure handling policies for command groups.
   */
  enum class failure_policy_e {
    FAIL_FAST,              ///< Stop execution on first failure
    CONTINUE_ON_FAILURE     ///< Continue execution despite failures
  };

  /**
   * @brief Structure representing a single command within a command group.
   */
  struct command_t {
    std::string cmd;        ///< The command to execute
    bool elevated;          ///< Whether to run with elevated privileges
    int timeout_seconds;    ///< Command timeout in seconds
    bool ignore_error;      ///< Whether to ignore command errors
    bool async;             ///< Whether to run asynchronously (fire and forget)
    
    command_t() : elevated(false), timeout_seconds(30), ignore_error(false), async(false) {}
    command_t(const std::string &cmd, bool elevated = false, int timeout_seconds = 30, bool ignore_error = false, bool async = false)
        : cmd(cmd), elevated(elevated), timeout_seconds(timeout_seconds), ignore_error(ignore_error), async(async) {}
    command_t(std::string &&cmd, bool elevated = false, int timeout_seconds = 30, bool ignore_error = false, bool async = false)
        : cmd(std::move(cmd)), elevated(elevated), timeout_seconds(timeout_seconds), ignore_error(ignore_error), async(async) {}
  };

  /**
   * @brief Structure representing a group of commands that execute together.
   */
  struct command_group_t {
    std::string name;                     ///< Human-readable name for the group
    failure_policy_e failure_policy;     ///< How to handle command failures
    std::vector<command_t> commands;      ///< Commands in this group
    
    command_group_t() : failure_policy(failure_policy_e::FAIL_FAST) {}
  };

  /**
   * @brief Structure representing all event-actions for a specific stage.
   */
  struct stage_commands_t {
    std::vector<command_group_t> groups;  ///< Command groups for this stage
  };

  /**
   * @brief Structure representing all event-actions across all stages.
   */
  struct event_actions_t {
    std::unordered_map<stage_e, stage_commands_t> stages;  ///< Commands by stage
  };

  /**
   * @brief Structure representing per-app event-action configuration.
   */
  struct app_event_config_t {
    event_actions_t commands;                               ///< App-specific commands
    std::unordered_set<stage_e> excluded_global_stages;    ///< Global stages to exclude
  };

  /**
   * @brief Context information passed to event-actions during execution.
   */
  struct execution_context_t {
    std::string app_id;                   ///< Application ID
    std::string app_name;                 ///< Application name
    int client_count;                     ///< Number of connected clients
    stage_e current_stage;                ///< Current execution stage
    std::unordered_map<std::string, std::string> env_vars;  ///< Environment variables
  };

  /**
   * @brief Main event-action handler class.
   */
  class event_action_handler_t {
  public:
    /**
     * @brief Initialize the event-action handler.
     * @param global_commands Global event-action commands configuration
     */
    void initialize(const event_actions_t &global_commands);

    /**
     * @brief Set app-specific event-actions.
     * @param app_id Application ID
     * @param app_config App-specific event-action configuration
     */
    void set_app_commands(int app_id, const app_event_config_t &app_config);

    /**
     * @brief Execute event-actions for a specific stage.
     * @param stage The execution stage
     * @param context Execution context information
     * @return 0 on success, negative on failure
     */
    int execute_stage(stage_e stage, const execution_context_t &context);

    /**
     * @brief Convert legacy do/undo commands to new format.
     * @param legacy_commands Legacy prep commands
     * @return Converted event-actions
     */

    /**
     * @brief Get stage name as string.
     * @param stage The stage enum value
     * @return String representation of the stage
     */
    static std::string stage_to_string(stage_e stage);

    /**
     * @brief Parse stage from string.
     * @param stage_str String representation of the stage
     * @return Stage enum value, or nullopt if invalid
     */
    static std::optional<stage_e> string_to_stage(const std::string &stage_str);

  private:
    event_actions_t _global_commands;
    std::unordered_map<int, app_event_config_t> _app_commands;

    /**
     * @brief Execute a command group.
     * @param group The command group to execute
     * @param context Execution context
     * @return 0 on success, negative on failure
     */
    int execute_group(const command_group_t &group, const execution_context_t &context);

    /**
     * @brief Execute a single command.
     * @param command The command to execute
     * @param context Execution context
     * @return 0 on success, negative on failure
     */
    int execute_command(const command_t &command, const execution_context_t &context);

    /**
     * @brief Get all command groups for a stage, merging global and app-specific.
     * @param stage The execution stage
     * @param app_id Application ID
     * @return Vector of command groups to execute
     */
    std::vector<command_group_t> get_stage_groups(stage_e stage, int app_id);
  };

  // Global instance
  extern event_action_handler_t event_handler;

  /**
   * @brief Get stage name as string (free function).
   * @param stage The stage enum value
   * @return String representation of the stage
   */
  std::string stage_to_string(stage_e stage);

  /**
   * @brief Helper functions for common stage execution.
   */
  namespace stages {
    int execute_pre_stream_start(const execution_context_t &context);
    int execute_post_stream_start(const execution_context_t &context);
    int execute_pre_display_check(const execution_context_t &context);
    int execute_post_display_check(const execution_context_t &context);
    int execute_additional_client(const execution_context_t &context);
    int execute_stream_resume(const execution_context_t &context);
    int execute_stream_pause(const execution_context_t &context);
    int execute_pre_stream_stop(const execution_context_t &context);
    int execute_pre_display_cleanup(const execution_context_t &context);
    int execute_post_display_cleanup(const execution_context_t &context);
    int execute_post_stream_stop(const execution_context_t &context);
    int execute_additional_client_disconnect(const execution_context_t &context);
  }

} // namespace event_actions
