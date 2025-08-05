
/**
 * @file process_handler.h
 * @brief Defines the ProcessHandler class for managing Windows processes.
 *
 * Provides an interface for launching, waiting, terminating, and cleaning up Windows processes.
 */

#pragma once

// standard includes
#include <string>

// local includes
#include "src/platform/windows/ipc/misc_utils.h"

// platform includes
#include <windows.h>

/**
 * @brief Handles launching, waiting, and terminating a Windows process.
 *
 * Provides methods to start a process, wait for its completion, terminate it, and clean up resources.
 */
class ProcessHandler {
public:
  /**
   * @brief Starts a process with the given application path and arguments.
   *
   * - Launches the specified application with provided arguments.
   *
   * - Initializes process information for later management.
   *
   * @param application The full path to the executable to launch.
   *
   * @param arguments The command-line arguments to pass to the process.
   *
   * @returns true if the process was started successfully, false otherwise.
   */
  bool start(const std::wstring &application, std::wstring_view arguments);

  /**
   * @brief Waits for the process to finish and retrieves its exit code.
   *
   * - Waits for the launched process to exit.
   *
   * - Retrieves the exit code of the process.
   *
   * @param exitCode Reference to a DWORD to receive the process exit code.
   *
   * @returns true if the process finished successfully, false otherwise.
   */
  bool wait(DWORD &exitCode);

  /**
   * @brief Terminates the process if it is running.
   *
   * - Checks if the process is active.
   *
   * - Attempts to terminate the process.
   */
  void terminate();

  /**
   * @brief Constructs a new ProcessHandler object.
   *
   * Initializes internal state and prepares for process management.
   */
  ProcessHandler();

  /**
   * @brief Cleans up process handles and resources.
   */
  ~ProcessHandler();

  /**
   * @brief Gets the process handle for the running process.
   * @return HANDLE to the process, or nullptr if no process is running.
   */
  HANDLE get_process_handle() const;

private:
  PROCESS_INFORMATION pi_ {};
  bool running_ = false;
  platf::dxgi::safe_handle job_;
};

/**
 * @brief Creates a Windows Job object that ensures all associated processes are terminated when the last handle is closed.
 * This function creates a job object with the "kill on job close" limit set. Any process assigned to this job will be automatically terminated when the job handle is closed.
 * @returns A safe_handle wrapping the created job object, or an invalid handle if creation fails.
 */
platf::dxgi::safe_handle create_kill_on_close_job();
