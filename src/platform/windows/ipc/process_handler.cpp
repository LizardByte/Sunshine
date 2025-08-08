
/**
 * @file process_handler.cpp
 * @brief Implements the ProcessHandler class for managing process creation and control on Windows.
 *
 * This file provides the implementation for starting, waiting, and terminating processes,
 * including support for attribute lists and impersonation as needed for the Sunshine project.
 */

// platform includes
#include <windows.h>

// standard includes
#include <algorithm>
#include <system_error>
#include <vector>

// local includes
#include "process_handler.h"
#include "src/platform/windows/misc.h"
#include "src/utility.h"

ProcessHandler::ProcessHandler():
    job_(create_kill_on_close_job()) {}

bool ProcessHandler::start(const std::wstring &application_path, std::wstring_view arguments) {
  if (running_) {
    return false;
  }

  std::error_code error_code;
  HANDLE job_handle = job_ ? job_.get() : nullptr;
  STARTUPINFOEXW startup_info = platf::create_startup_info(nullptr, &job_handle, error_code);
  if (error_code) {
    return false;
  }

  auto fail_guard = util::fail_guard([&]() {
    if (startup_info.lpAttributeList) {
      platf::free_proc_thread_attr_list(startup_info.lpAttributeList);
    }
  });

  ZeroMemory(&pi_, sizeof(pi_));

  // Build command line: app path + space + arguments
  auto command = std::wstring(application_path);
  if (!arguments.empty()) {
    command += L" ";
    command += std::wstring(arguments);
  }

  DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;

  std::string command_str = platf::to_utf8(command);
  std::wstring working_dir;  // Empty working directory

  bool result;
  if (platf::is_running_as_system()) {
    result = platf::launch_process_with_impersonation(false, command_str, working_dir, creation_flags, startup_info, pi_, error_code);
  } else {
    result = platf::launch_process_without_impersonation(command_str, working_dir, creation_flags, startup_info, pi_, error_code);
  }

  running_ = result && !error_code;
  if (!running_) {
    ZeroMemory(&pi_, sizeof(pi_));
  }
  return running_;
}

bool ProcessHandler::wait(DWORD &exit_code) {
  if (!running_ || pi_.hProcess == nullptr) {
    return false;
  }
  DWORD wait_result = WaitForSingleObject(pi_.hProcess, INFINITE);
  if (wait_result != WAIT_OBJECT_0) {
    return false;
  }
  BOOL got_code = GetExitCodeProcess(pi_.hProcess, &exit_code);
  running_ = false;
  return got_code != 0;
}

void ProcessHandler::terminate() {
  if (running_ && pi_.hProcess) {
    TerminateProcess(pi_.hProcess, 1);
    running_ = false;
  }
}

ProcessHandler::~ProcessHandler() {
  // Terminate process first if it's still running
  terminate();

  // Clean up handles
  if (pi_.hProcess) {
    CloseHandle(pi_.hProcess);
  }
  if (pi_.hThread) {
    CloseHandle(pi_.hThread);
  }
  // job_ is a safe_handle and will auto-cleanup
}

HANDLE ProcessHandler::get_process_handle() const {
  return running_ ? pi_.hProcess : nullptr;
}

platf::dxgi::safe_handle create_kill_on_close_job() {
  HANDLE job_handle = CreateJobObjectW(nullptr, nullptr);
  if (!job_handle) {
    return {};
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info = {};
  job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job_handle, JobObjectExtendedLimitInformation, &job_info, sizeof(job_info))) {
    CloseHandle(job_handle);
    return {};
  }
  return platf::dxgi::safe_handle(job_handle);
}
