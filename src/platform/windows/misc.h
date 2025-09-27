/**
 * @file src/platform/windows/misc.h
 * @brief Miscellaneous declarations for Windows.
 */
#pragma once

// standard includes
#include <chrono>
#include <functional>
#include <string_view>
#include <system_error>

// platform includes
#include <Windows.h>
#include <winnt.h>

namespace platf {
  void print_status(const std::string_view &prefix, HRESULT status);
  HDESK syncThreadDesktop();

  int64_t qpc_counter();

  std::chrono::nanoseconds qpc_time_difference(int64_t performance_counter1, int64_t performance_counter2);

  /**
   * @brief Convert a UTF-8 string into a UTF-16 wide string.
   * @param string The UTF-8 string.
   * @return The converted UTF-16 wide string.
   */
  std::wstring from_utf8(const std::string &string);

  /**
   * @brief Convert a UTF-16 wide string into a UTF-8 string.
   * @param string The UTF-16 wide string.
   * @return The converted UTF-8 string.
   */
  std::string to_utf8(const std::wstring &string);

  /**
   * @brief Check if the current process is running with system-level privileges.
   * @return `true` if the current process has system-level privileges, `false` otherwise.
   */
  bool is_running_as_system();

  /**
   * @brief Launch a process with user impersonation (for use when running as SYSTEM).
   * @param elevated Specify whether to elevate the process.
   * @param cmd The command to run.
   * @param start_dir The working directory for the new process.
   * @param creation_flags The creation flags for CreateProcess().
   * @param startup_info The startup info structure for the new process.
   * @param process_info The process information structure to receive results.
   * @param ec A reference to an error code that will store any error that occurred.
   * @return `true` if the process was launched successfully, `false` otherwise.
   */
  bool launch_process_with_impersonation(bool elevated, const std::string &cmd, const std::wstring &start_dir, DWORD creation_flags, STARTUPINFOEXW &startup_info, PROCESS_INFORMATION &process_info, std::error_code &ec);

  /**
   * @brief Launch a process without impersonation (for use when running as regular user).
   * @param cmd The command to run.
   * @param start_dir The working directory for the new process.
   * @param creation_flags The creation flags for CreateProcess().
   * @param startup_info The startup info structure for the new process.
   * @param process_info The process information structure to receive results.
   * @param ec A reference to an error code that will store any error that occurred.
   * @return `true` if the process was launched successfully, `false` otherwise.
   */
  bool launch_process_without_impersonation(const std::string &cmd, const std::wstring &start_dir, DWORD creation_flags, STARTUPINFOEXW &startup_info, PROCESS_INFORMATION &process_info, std::error_code &ec);

  /**
   * @brief Create a `STARTUPINFOEXW` structure for launching a process.
   * @param file A pointer to a `FILE` object that will be used as the standard output and error for the new process, or null if not needed.
   * @param job A job object handle to insert the new process into. This pointer must remain valid for the life of this startup info!
   * @param ec A reference to a `std::error_code` object that will store any error that occurred during the creation of the structure.
   * @return A structure that contains information about how to launch the new process.
   */
  STARTUPINFOEXW create_startup_info(FILE *file, HANDLE *job, std::error_code &ec);

  /**
   * @brief Free the attribute list allocated in create_startup_info.
   * @param list The attribute list to free.
   */
  void free_proc_thread_attr_list(LPPROC_THREAD_ATTRIBUTE_LIST list);

  /**
   * @brief Obtain the current sessions user's primary token with elevated privileges.
   * @return The user's token. If user has admin capability it will be elevated, otherwise it will be a limited token. On error, `nullptr`.
   */
  HANDLE retrieve_users_token(bool elevated);

  /**
   * @brief Retrieves the parent process ID of the current process.
   *
   * @return DWORD The process ID of the parent process, or 0 if the parent could not be determined.
   */
  DWORD get_parent_process_id();
  /**
   * @brief Retrieves the parent process ID of the specified process.
   *
   * @param process_id The process ID of the process whose parent process ID is to be retrieved.
   * @return DWORD The process ID of the parent process, or 0 if the parent could not be determined.
   */
  DWORD get_parent_process_id(DWORD process_id);

}  // namespace platf
