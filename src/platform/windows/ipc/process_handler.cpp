
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

bool ProcessHandler::start(const std::wstring &application, std::wstring_view arguments) {
  if (running_) {
    return false;
  }

  std::error_code ec;
  HANDLE job_handle = job_ ? job_.get() : nullptr;
  STARTUPINFOEXW startup_info = platf::create_startup_info(nullptr, &job_handle, ec);
  if (ec) {
    return false;
  }

  auto guard = util::fail_guard([&]() {
    if (startup_info.lpAttributeList) {
      platf::free_proc_thread_attr_list(startup_info.lpAttributeList);
    }
  });

  ZeroMemory(&pi_, sizeof(pi_));

  // Build command line: app path + space + arguments
  auto cmd = std::wstring(application);
  if (!arguments.empty()) {
    cmd += L" ";
    cmd += std::wstring(arguments);
  }

  DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;

  std::string cmd_str = platf::to_utf8(cmd);
  std::wstring working_dir;  // Empty working directory

  bool result;
  if (platf::is_running_as_system()) {
    result = platf::launch_process_with_impersonation(false, cmd_str, working_dir, creation_flags, startup_info, pi_, ec);
  } else {
    result = platf::launch_process_without_impersonation(cmd_str, working_dir, creation_flags, startup_info, pi_, ec);
  }

  running_ = result && !ec;
  if (!running_) {
    ZeroMemory(&pi_, sizeof(pi_));
  }
  return running_;
}

bool ProcessHandler::wait(DWORD &exitCode) {
  if (!running_ || pi_.hProcess == nullptr) {
    return false;
  }
  if (DWORD waitResult = WaitForSingleObject(pi_.hProcess, INFINITE); waitResult != WAIT_OBJECT_0) {
    return false;
  }
  BOOL gotCode = GetExitCodeProcess(pi_.hProcess, &exitCode);
  running_ = false;
  return gotCode != 0;
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
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) {
    return {};
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
  info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
    CloseHandle(job);
    return {};
  }
  return platf::dxgi::safe_handle(job);
}
