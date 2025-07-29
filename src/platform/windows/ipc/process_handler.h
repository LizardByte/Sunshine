
/**
 * @file process_handler.h
 * @brief Defines the ProcessHandler class for managing Windows processes.
 *
 * Provides an interface for launching, waiting, terminating, and cleaning up Windows processes.
 */

#pragma once

// standard includes
#include <string>

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
   * @brief Cleans up process handles and resources.
   */
  ~ProcessHandler();

private:
  PROCESS_INFORMATION pi_ {};
  bool running_ = false;
};
