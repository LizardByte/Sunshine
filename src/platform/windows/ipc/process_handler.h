/**
 * @file src/platform/windows/ipc/process_handler.h
 * @brief Windows helper process management utilities.
 */
#pragma once

// standard includes
#include <string>

// local includes
#include "src/platform/windows/ipc/misc_utils.h"

// platform includes
#include <windows.h>

/**
 * @brief RAII wrapper for launching and controlling a Windows helper process.
 *
 * Provides minimal operations needed by the WGC capture helper: start, wait, terminate
 * and access to the native process handle. Ensures handles are cleaned up on destruction.
 */
class ProcessHandler {
public:
  /**
   * @brief Construct an empty handler (no process started).
   */
  ProcessHandler();

  /**
   * @brief Destroy the handler and release any process / job handles.
   */
  ~ProcessHandler();

  /**
   * @brief Launch the target executable with arguments if no process is running.
   * @param application_path Full path to executable.
   * @param arguments Command line arguments (not including the executable path).
   * @return `true` on successful launch, `false` otherwise.
   */
  bool start(const std::wstring &application_path, std::wstring_view arguments);

  /**
   * @brief Block until the process exits and obtain its exit code.
   * @param exit_code Receives process exit code on success.
   * @return `true` if the process was running and exited cleanly; `false` otherwise.
   */
  bool wait(DWORD &exit_code);

  /**
   * @brief Terminate the process if still running (best-effort).
   */
  void terminate();

  /**
   * @brief Get the native HANDLE of the managed process.
   * @return Process HANDLE or `nullptr` if not running.
   */
  HANDLE get_process_handle() const;

private:
  PROCESS_INFORMATION pi_ {};
  bool running_ = false;
  platf::dxgi::safe_handle job_;
};

/**
 * @brief Create a Job object configured to kill remaining processes on last handle close.
 * @return Valid `safe_handle` on success, otherwise invalid.
 */
platf::dxgi::safe_handle create_kill_on_close_job();
