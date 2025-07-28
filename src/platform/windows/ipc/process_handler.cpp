#include "process_handler.h"

#include "../misc.h"

#include <algorithm>
#include <system_error>
#include <vector>

bool ProcessHandler::start(const std::wstring& application, std::wstring_view arguments) {
  if (running_) {
    return false;
  }

  std::error_code ec;
  STARTUPINFOEXW startup_info = platf::create_startup_info(nullptr, nullptr, ec);
  if (ec) {
    return false;
  }

  // Use a simple scope guard to ensure the attribute list is freed
  struct AttrListGuard {
    LPPROC_THREAD_ATTRIBUTE_LIST list;

    // Explicit constructor
    explicit AttrListGuard(LPPROC_THREAD_ATTRIBUTE_LIST l) : list(l) {}

    // Delete copy constructor and copy assignment operator
    AttrListGuard(const AttrListGuard&) = delete;
    AttrListGuard& operator=(const AttrListGuard&) = delete;

    // Define move constructor
    AttrListGuard(AttrListGuard&& other) noexcept : list(other.list) {
      other.list = nullptr;
    }

    // Define move assignment operator
    AttrListGuard& operator=(AttrListGuard&& other) noexcept {
      if (this != &other) {
        if (list) {
          platf::free_proc_thread_attr_list(list);
        }
        list = other.list;
        other.list = nullptr;
      }
      return *this;
    }

    ~AttrListGuard() {
      if (list) {
        platf::free_proc_thread_attr_list(list);
      }
    }
  };
  AttrListGuard guard{startup_info.lpAttributeList};

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
  if (pi_.hProcess) {
    CloseHandle(pi_.hProcess);
  }
  if (pi_.hThread) {
    CloseHandle(pi_.hThread);
  }
}
