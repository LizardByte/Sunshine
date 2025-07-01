/**
 * @file src/event_actions.cpp
 * @brief Implementation of the comprehensive event-action system.
 */

// standard includes
#include <chrono>
#include <sstream>
#include <thread>

// lib includes
#include <boost/filesystem.hpp>
#include <boost/process.hpp>

// local includes
#include "event_actions.h"
#include "logging.h"
#include "platform/common.h"
#include "utility.h"

using namespace std::literals;

namespace event_actions {

  // Global instance
  event_action_handler_t event_handler;

  void event_action_handler_t::initialize(const event_actions_t &global_commands) {
    _global_commands = global_commands;
    BOOST_LOG(info) << "Event-action handler initialized with " << _global_commands.stages.size() << " global stages";
  }

  void event_action_handler_t::set_app_commands(int app_id, const app_event_config_t &app_config) {
    _app_commands[app_id] = app_config;
    BOOST_LOG(debug) << "Set event-actions for app " << app_id << " with " 
                     << app_config.commands.stages.size() << " stages, excluding " 
                     << app_config.excluded_global_stages.size() << " global stages";
  }

  int event_action_handler_t::execute_stage(stage_e stage, const execution_context_t &context) {
    BOOST_LOG(info) << "Executing event-action stage: " << stage_to_string(stage) 
                    << " for app " << context.app_name << " (ID: " << context.app_id << ")";

    // Only execute event-actions when there's an active app session
    // Global actions should only run in the context of an app
    if (context.app_id.empty() || context.app_id == "-1") {
      BOOST_LOG(debug) << "No active app session, skipping event-action stage: " << stage_to_string(stage);
      return 0;
    }

    if(stage == stage_e::STREAM_PAUSE || stage == stage_e::STREAM_RESUME){
      BOOST_LOG(debug) << "Debug: Entered " << stage_to_string(stage) 
               << " stage for app " << context.app_name << " (ID: " << context.app_id << ")";

        [[maybe_unused]] volatile int debug_breakpoint = 0; // Place breakpoint here if needed
    }
    // Create a mutable copy of the context to set the stage
    execution_context_t stage_context = context;
    stage_context.current_stage = stage;

    int app_id_int = std::stoi(context.app_id);
    auto groups = get_stage_groups(stage, app_id_int);
    if (groups.empty()) {
      BOOST_LOG(debug) << "No commands for stage " << stage_to_string(stage);
      BOOST_LOG(info) << "No command groups found for stage " << stage_to_string(stage) << ", nothing to execute.";
      return 0;
    }

    BOOST_LOG(debug) << "Found " << groups.size() << " command groups for stage " << stage_to_string(stage);

    bool any_group_executed = false;
    for (const auto &group : groups) {
      int result = execute_group(group, stage_context);
      if (result != 0) {
        BOOST_LOG(error) << "Command group '" << group.name << "' failed with code " << result;
        return result;
      }
      any_group_executed = true;
    }
    if (!any_group_executed) {
      BOOST_LOG(info) << "All command groups for stage " << stage_to_string(stage) << " were skipped or empty.";
    }

    BOOST_LOG(info) << "Successfully completed event-action stage: " << stage_to_string(stage);
    return 0;
  }

  int event_action_handler_t::execute_group(const command_group_t &group, const execution_context_t &context) {
    BOOST_LOG(debug) << "Executing command group: " << group.name 
                     << " (policy: " << (group.failure_policy == failure_policy_e::FAIL_FAST ? "fail-fast" : "continue") 
                     << ", commands: " << group.commands.size() << ")";

    bool any_command_executed = false;
    for (const auto &command : group.commands) {
      if (command.cmd.empty()) {
        BOOST_LOG(debug) << "Skipping empty command in group " << group.name;
        continue;
      }

      int result = execute_command(command, context);
      if (result != 0) {
        BOOST_LOG(error) << "Command failed in group '" << group.name << "': " << command.cmd;
        
        if (group.failure_policy == failure_policy_e::FAIL_FAST) {
          BOOST_LOG(error) << "Stopping execution due to fail-fast policy";
          return result;
        } else {
          BOOST_LOG(warning) << "Continuing execution despite failure due to continue-on-failure policy";
        }
      }
      any_command_executed = true;
    }
    if (!any_command_executed) {
      BOOST_LOG(info) << "No commands executed in group '" << group.name << "' (all empty or skipped).";
    }

    BOOST_LOG(debug) << "Command group '" << group.name << "' completed successfully";
    return 0;
  }

  int event_action_handler_t::execute_command(const command_t &command, const execution_context_t &context) {
    BOOST_LOG(info) << "Executing event-action command: [" << command.cmd << "] "
                    << (command.elevated ? "(elevated) " : "")
                    << (command.async ? "(async) " : "")
                    << (command.ignore_error ? "(ignore-error) " : "")
                    << "(timeout: " << command.timeout_seconds << "s)";

    // Create environment with context variables
    auto env = boost::this_process::environment();
    for (const auto &[key, value] : context.env_vars) {
      env[key] = value;
      BOOST_LOG(debug) << "Set environment variable for command: " << key << "=" << value;
    }

    // Add event-action specific environment variables
    env["SUNSHINE_EVENT_STAGE"] = stage_to_string(context.current_stage);
    env["SUNSHINE_EVENT_APP_ID"] = context.app_id;
    env["SUNSHINE_EVENT_APP_NAME"] = context.app_name;
    env["SUNSHINE_EVENT_CLIENT_COUNT"] = std::to_string(context.client_count);
    BOOST_LOG(debug) << "Set SUNSHINE_EVENT_STAGE=" << stage_to_string(context.current_stage)
                     << ", SUNSHINE_EVENT_APP_ID=" << context.app_id
                     << ", SUNSHINE_EVENT_APP_NAME=" << context.app_name
                     << ", SUNSHINE_EVENT_CLIENT_COUNT=" << context.client_count;

    // Find working directory
    boost::filesystem::path working_dir;
    try {
      working_dir = boost::filesystem::current_path();
    } catch (const std::exception &e) {
      BOOST_LOG(warning) << "Could not determine current directory, using root: " << e.what();
      working_dir = "/";
    }

    std::error_code ec;
    BOOST_LOG(debug) << "Running command in directory: " << working_dir.string();

    auto child = platf::run_command(command.elevated, true, command.cmd, working_dir, env, nullptr, ec, nullptr);

    if (ec) {
      BOOST_LOG(error) << "Failed to start command [" << command.cmd << "]: " << ec.message();
      if (command.ignore_error || command.async) {
        BOOST_LOG(warning) << "Ignoring command startup failure due to ignore_error/async setting";
        return 0;
      }
      return -1;
    }

    // For async commands, don't wait for completion
    if (command.async) {
      BOOST_LOG(debug) << "Command [" << command.cmd << "] started asynchronously (fire and forget)";
      return 0;
    }

    // Wait for completion with timeout
    bool completed = false;
    std::chrono::seconds timeout(command.timeout_seconds);
    auto start_time = std::chrono::steady_clock::now();

    while (!completed && (std::chrono::steady_clock::now() - start_time) < timeout) {
      if (!child.running()) {
        completed = true;
        break;
      }
      std::this_thread::sleep_for(100ms);
    }

    if (!completed) {
      BOOST_LOG(error) << "Command [" << command.cmd << "] timed out after " << command.timeout_seconds << " seconds";
      child.terminate();
      child.wait();
      if (command.ignore_error) {
        BOOST_LOG(warning) << "Ignoring command timeout due to ignore_error setting";
        return 0;
      }
      return -2;
    }

    child.wait();
    auto exit_code = child.exit_code();

    if (exit_code != 0) {
      BOOST_LOG(error) << "Command [" << command.cmd << "] failed with exit code " << exit_code;
      if (command.ignore_error) {
        BOOST_LOG(warning) << "Ignoring command failure due to ignore_error setting";
        return 0;
      }
      return exit_code;
    }

    BOOST_LOG(debug) << "Command [" << command.cmd << "] completed successfully";
    return 0;
  }

  std::vector<command_group_t> event_action_handler_t::get_stage_groups(stage_e stage, int app_id) {
    std::vector<command_group_t> groups;

    // Add global commands for this stage (if not excluded by app)
    auto app_it = _app_commands.find(app_id);
    bool app_excludes_global = (app_it != _app_commands.end() && 
                                app_it->second.excluded_global_stages.count(stage) > 0);

    if (!app_excludes_global) {
      auto global_stage_it = _global_commands.stages.find(stage);
      if (global_stage_it != _global_commands.stages.end()) {
        for (const auto &group : global_stage_it->second.groups) {
          groups.push_back(group);
        }
      }
    }

    // Add app-specific commands for this stage
    if (app_it != _app_commands.end()) {
      auto app_stage_it = app_it->second.commands.stages.find(stage);
      if (app_stage_it != app_it->second.commands.stages.end()) {
        for (const auto &group : app_stage_it->second.groups) {
          groups.push_back(group);
        }
      }
    }

    return groups;
  }

  std::string event_action_handler_t::stage_to_string(stage_e stage) {
    switch (stage) {
      case stage_e::PRE_STREAM_START:         return "PRE_STREAM_START";
      case stage_e::POST_STREAM_START:        return "POST_STREAM_START";
      case stage_e::PRE_DISPLAY_CHECK:        return "PRE_DISPLAY_CHECK";
      case stage_e::POST_DISPLAY_CHECK:       return "POST_DISPLAY_CHECK";
      case stage_e::ADDITIONAL_CLIENT:        return "ADDITIONAL_CLIENT";
      case stage_e::STREAM_RESUME:            return "STREAM_RESUME";
      case stage_e::STREAM_PAUSE:             return "STREAM_PAUSE";
      case stage_e::PRE_STREAM_STOP:          return "PRE_STREAM_STOP";
      case stage_e::POST_STREAM_STOP:         return "POST_STREAM_STOP";
      case stage_e::ADDITIONAL_CLIENT_DISCONNECT: return "ADDITIONAL_CLIENT_DISCONNECT";
      default:                                return "UNKNOWN";
    }
  }

  std::optional<stage_e> event_action_handler_t::string_to_stage(const std::string &stage_str) {
    static const std::unordered_map<std::string, stage_e> stage_map = {
      {"PRE_STREAM_START", stage_e::PRE_STREAM_START},
      {"POST_STREAM_START", stage_e::POST_STREAM_START},
      {"PRE_DISPLAY_CHECK", stage_e::PRE_DISPLAY_CHECK},
      {"POST_DISPLAY_CHECK", stage_e::POST_DISPLAY_CHECK},
      {"ADDITIONAL_CLIENT", stage_e::ADDITIONAL_CLIENT},
      {"STREAM_RESUME", stage_e::STREAM_RESUME},
      {"STREAM_PAUSE", stage_e::STREAM_PAUSE},
      {"PRE_STREAM_STOP", stage_e::PRE_STREAM_STOP},
      {"POST_STREAM_STOP", stage_e::POST_STREAM_STOP},
      {"ADDITIONAL_CLIENT_DISCONNECT", stage_e::ADDITIONAL_CLIENT_DISCONNECT}
    };

    auto it = stage_map.find(stage_str);
    return (it != stage_map.end()) ? std::optional<stage_e>(it->second) : std::nullopt;
  }

  // Free function version of stage_to_string
  std::string stage_to_string(stage_e stage) {
    return event_action_handler_t::stage_to_string(stage);
  }

  // Helper functions for common stage execution
  namespace stages {
    int execute_pre_stream_start(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::PRE_STREAM_START, context);
    }

    int execute_post_stream_start(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::POST_STREAM_START, context);
    }

    int execute_pre_display_check(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::PRE_DISPLAY_CHECK, context);
    }

    int execute_post_display_check(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::POST_DISPLAY_CHECK, context);
    }

    int execute_additional_client(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::ADDITIONAL_CLIENT, context);
    }

    int execute_stream_resume(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::STREAM_RESUME, context);
    }

    int execute_stream_pause(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::STREAM_PAUSE, context);
    }

    int execute_pre_stream_stop(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::PRE_STREAM_STOP, context);
    }

    int execute_post_stream_stop(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::POST_STREAM_STOP, context);
    }

    int execute_additional_client_disconnect(const execution_context_t &context) {
      return event_handler.execute_stage(stage_e::ADDITIONAL_CLIENT_DISCONNECT, context);
    }
  }

} // namespace event_actions
