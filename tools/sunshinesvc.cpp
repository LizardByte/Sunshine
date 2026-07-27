/**
 * @file tools/sunshinesvc.cpp
 * @brief Handles launching Sunshine.exe into user sessions as SYSTEM
 */
#define WIN32_LEAN_AND_MEAN
#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <lizardbyte/common/env.h>
#include <string>
#include <Windows.h>
#include <WtsApi32.h>

// PROC_THREAD_ATTRIBUTE_JOB_LIST is currently missing from MinGW headers
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
  #define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

SERVICE_STATUS_HANDLE service_status_handle;
SERVICE_STATUS service_status;
HANDLE stop_event;
HANDLE session_change_event;

constexpr auto SERVICE_NAME = "SunshineService";
constexpr auto SERVICE_READY_EVENT_ENV = "SUNSHINE_SERVICE_READY_EVENT";
constexpr DWORD SERVICE_START_WAIT_HINT = 30 * 1000;
constexpr DWORD SUNSHINE_READY_TIMEOUT = 60 * 1000;
constexpr DWORD SUNSHINE_STARTUP_RETRY_LIMIT = 6;

/**
 * @brief Report to the Windows Service Control Manager that startup is still in progress.
 */
void ReportServiceStartPending() {
  service_status.dwCurrentState = SERVICE_START_PENDING;
  service_status.dwControlsAccepted = 0;
  service_status.dwWin32ExitCode = NO_ERROR;
  service_status.dwWaitHint = SERVICE_START_WAIT_HINT;
  ++service_status.dwCheckPoint;
  SetServiceStatus(service_status_handle, &service_status);
}

/**
 * @brief Report to the Windows Service Control Manager that the service is running.
 */
void ReportServiceRunning() {
  service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
  service_status.dwCurrentState = SERVICE_RUNNING;
  service_status.dwWin32ExitCode = NO_ERROR;
  service_status.dwWaitHint = 0;
  service_status.dwCheckPoint = 0;
  SetServiceStatus(service_status_handle, &service_status);
}

/**
 * @brief Report to the Windows Service Control Manager that the service has stopped.
 *
 * @param win32_exit_code Exit code reported for the stopped service.
 */
void ReportServiceStopped(DWORD win32_exit_code = NO_ERROR) {
  service_status.dwControlsAccepted = 0;
  service_status.dwCurrentState = SERVICE_STOPPED;
  service_status.dwWin32ExitCode = win32_exit_code;
  service_status.dwWaitHint = 0;
  service_status.dwCheckPoint = 0;
  SetServiceStatus(service_status_handle, &service_status);
}

/**
 * @brief State observed while waiting for Sunshine.exe to signal readiness.
 */
enum class SunshineReadyResult : std::uint8_t {
  ready,  ///< Sunshine.exe signaled that its Web UI is accepting connections.
  stop,  ///< The service received a stop request.
  exited,  ///< Sunshine.exe exited before signaling readiness.
  session_changed,  ///< The active console session changed before readiness.
  timed_out  ///< Sunshine.exe did not signal readiness before the timeout.
};

DWORD WINAPI HandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
  switch (dwControl) {
    case SERVICE_CONTROL_INTERROGATE:
      return NO_ERROR;

    case SERVICE_CONTROL_SESSIONCHANGE:
      // If a new session connects to the console, restart Sunshine
      // to allow it to spawn inside the new console session.
      if (dwEventType == WTS_CONSOLE_CONNECT && session_change_event != nullptr) {
        SetEvent(session_change_event);
      }
      return NO_ERROR;

    case SERVICE_CONTROL_PRESHUTDOWN:
      // The system is shutting down
    case SERVICE_CONTROL_STOP:
      // Let SCM know we're stopping in up to 30 seconds
      service_status.dwCurrentState = SERVICE_STOP_PENDING;
      service_status.dwControlsAccepted = 0;
      service_status.dwWaitHint = 30 * 1000;
      SetServiceStatus(service_status_handle, &service_status);

      // Trigger ServiceMain() to start cleanup
      if (stop_event != nullptr) {
        SetEvent(stop_event);
      }
      return NO_ERROR;

    default:
      return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

HANDLE CreateJobObjectForChildProcess() {
  HANDLE job_handle = CreateJobObjectW(nullptr, nullptr);
  if (!job_handle) {
    return nullptr;
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limit_info = {};

  // Kill Sunshine.exe when the final job object handle is closed (which will happen if we terminate unexpectedly).
  // This ensures we don't leave an orphaned Sunshine.exe running with an inherited handle to our log file.
  job_limit_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

  // Allow Sunshine.exe to use CREATE_BREAKAWAY_FROM_JOB when spawning processes to ensure they can to live beyond
  // the lifetime of SunshineSvc.exe. This avoids unexpected user data loss if we crash or are killed.
  job_limit_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_BREAKAWAY_OK;

  if (!SetInformationJobObject(job_handle, JobObjectExtendedLimitInformation, &job_limit_info, sizeof(job_limit_info))) {
    CloseHandle(job_handle);
    return nullptr;
  }

  return job_handle;
}

LPPROC_THREAD_ATTRIBUTE_LIST AllocateProcThreadAttributeList(DWORD attribute_count) {
  SIZE_T size;
  InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &size);

  auto list = (LPPROC_THREAD_ATTRIBUTE_LIST) HeapAlloc(GetProcessHeap(), 0, size);
  if (list == nullptr) {
    return nullptr;
  }

  if (!InitializeProcThreadAttributeList(list, attribute_count, 0, &size)) {
    HeapFree(GetProcessHeap(), 0, list);
    return nullptr;
  }

  return list;
}

HANDLE DuplicateTokenForSession(DWORD console_session_id) {
  HANDLE current_token;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &current_token)) {
    return nullptr;
  }

  // Duplicate our own LocalSystem token
  HANDLE new_token;
  if (!DuplicateTokenEx(current_token, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &new_token)) {
    CloseHandle(current_token);
    return nullptr;
  }

  CloseHandle(current_token);

  // Change the duplicated token to the console session ID
  if (!SetTokenInformation(new_token, TokenSessionId, &console_session_id, sizeof(console_session_id))) {
    CloseHandle(new_token);
    return nullptr;
  }

  return new_token;
}

HANDLE OpenLogFileHandle() {
  WCHAR log_file_name[MAX_PATH];

  // Create sunshine.log in the Temp folder (usually %SYSTEMROOT%\Temp)
  GetTempPathW(_countof(log_file_name), log_file_name);
  wcscat_s(log_file_name, L"sunshine.log");

  // The file handle must be inheritable for our child process to use it
  SECURITY_ATTRIBUTES security_attributes = {sizeof(security_attributes), nullptr, TRUE};

  // Overwrite the old sunshine.log
  return CreateFileW(log_file_name, GENERIC_WRITE, FILE_SHARE_READ, &security_attributes, CREATE_ALWAYS, 0, nullptr);
}

bool RunTerminationHelper(HANDLE console_token, DWORD pid) {
  WCHAR module_path[MAX_PATH];
  GetModuleFileNameW(nullptr, module_path, _countof(module_path));
  std::wstring command;

  command += L'"';
  command += module_path;
  command += L'"';
  command += std::format(L" --terminate {}", pid);

  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  startup_info.lpDesktop = (LPWSTR) L"winsta0\\default";

  // Execute ourselves as a detached process in the user session with the --terminate argument.
  // This will allow us to attach to Sunshine's console and send it a Ctrl-C event.
  PROCESS_INFORMATION process_info;
  if (!CreateProcessAsUserW(console_token, module_path, (LPWSTR) command.c_str(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | DETACHED_PROCESS, nullptr, nullptr, &startup_info, &process_info)) {
    return false;
  }

  // Wait for the termination helper to complete
  WaitForSingleObject(process_info.hProcess, INFINITE);

  // Check the exit status of the helper process
  DWORD exit_code;
  GetExitCodeProcess(process_info.hProcess, &exit_code);

  // Cleanup handles
  CloseHandle(process_info.hProcess);
  CloseHandle(process_info.hThread);

  // If the helper process returned 0, it succeeded
  return exit_code == 0;
}

/**
 * @brief Gracefully terminate Sunshine.exe, falling back to forced termination.
 *
 * @param console_token Token for the console session where Sunshine.exe is running.
 * @param process_info Process information for the Sunshine.exe child process.
 */
void TerminateSunshineProcess(HANDLE console_token, PROCESS_INFORMATION &process_info) {
  // Try to gracefully terminate Sunshine.exe. If it doesn't terminate in 20 seconds, forcefully terminate it.
  if (!RunTerminationHelper(console_token, process_info.dwProcessId) || WaitForSingleObject(process_info.hProcess, 20000) != WAIT_OBJECT_0) {
    TerminateProcess(process_info.hProcess, ERROR_PROCESS_ABORTED);
  }
}

/**
 * @brief Wait for Sunshine.exe to signal that it has completed startup.
 *
 * @param process_handle Process handle for the Sunshine.exe child process.
 * @param ready_event Event signaled by Sunshine.exe after the Web UI starts listening.
 * @param console_session_id Console session ID where Sunshine.exe was launched.
 * @param report_start_pending Whether to keep reporting SERVICE_START_PENDING while waiting.
 * @return Observed child startup state.
 */
SunshineReadyResult WaitForSunshineReady(HANDLE process_handle, HANDLE ready_event, DWORD console_session_id, bool report_start_pending) {
  const auto started_at = GetTickCount64();
  using enum SunshineReadyResult;

  while (true) {
    if (report_start_pending) {
      ReportServiceStartPending();
    }

    const auto elapsed = GetTickCount64() - started_at;
    if (elapsed >= SUNSHINE_READY_TIMEOUT) {
      return SunshineReadyResult::timed_out;
    }

    const auto remaining = SUNSHINE_READY_TIMEOUT - elapsed;
    const auto wait_time = static_cast<DWORD>(remaining > 3000 ? 3000 : remaining);
    const std::array<HANDLE, 4> wait_objects {stop_event, process_handle, session_change_event, ready_event};

    switch (WaitForMultipleObjects(static_cast<DWORD>(wait_objects.size()), wait_objects.data(), FALSE, wait_time)) {
      case WAIT_OBJECT_0:
        return stop;

      case WAIT_OBJECT_0 + 1:
        return exited;

      case WAIT_OBJECT_0 + 2:
        if (WTSGetActiveConsoleSessionId() == console_session_id) {
          continue;
        }
        return session_changed;

      case WAIT_OBJECT_0 + 3:
        return ready;

      case WAIT_TIMEOUT:
        continue;

      default:
        return timed_out;
    }
  }
}

/**
 * @brief Release service-wide handles and the process attribute list.
 *
 * @param startup_info Startup info containing the optional process attribute list.
 * @param ready_event Event handle passed to Sunshine.exe for readiness signaling.
 * @param log_file_handle Log file handle inherited by Sunshine.exe.
 */
void CleanupServiceResources(STARTUPINFOEXW &startup_info, HANDLE &ready_event, HANDLE &log_file_handle) {
  if (startup_info.lpAttributeList != nullptr) {
    DeleteProcThreadAttributeList(startup_info.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, startup_info.lpAttributeList);
    startup_info.lpAttributeList = nullptr;
  }

  if (ready_event != nullptr) {
    CloseHandle(ready_event);
    ready_event = nullptr;
  }

  if (log_file_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(log_file_handle);
    log_file_handle = INVALID_HANDLE_VALUE;
  }

  if (session_change_event != nullptr) {
    CloseHandle(session_change_event);
    session_change_event = nullptr;
  }

  if (stop_event != nullptr) {
    CloseHandle(stop_event);
    stop_event = nullptr;
  }
}

/**
 * @brief Clean up service resources and report startup failure to the Service Control Manager.
 *
 * @param startup_info Startup info containing the optional process attribute list.
 * @param ready_event Event handle passed to Sunshine.exe for readiness signaling.
 * @param log_file_handle Log file handle inherited by Sunshine.exe.
 * @param win32_exit_code Exit code reported for the stopped service.
 */
void FailServiceStart(STARTUPINFOEXW &startup_info, HANDLE &ready_event, HANDLE &log_file_handle, DWORD win32_exit_code) {
  CleanupServiceResources(startup_info, ready_event, log_file_handle);
  ReportServiceStopped(win32_exit_code);
}

/**
 * @brief Convert a child process startup exit state into a service failure code.
 *
 * @param process_handle Process handle for the Sunshine.exe child process.
 * @return Exit code to report for the failed startup attempt.
 */
DWORD GetSunshineStartupFailureCode(HANDLE process_handle) {
  DWORD exit_code = ERROR_PROCESS_ABORTED;
  if (GetExitCodeProcess(process_handle, &exit_code) && exit_code == ERROR_SHUTDOWN_IN_PROGRESS) {
    SetEvent(stop_event);
  }

  return exit_code == NO_ERROR ? ERROR_PROCESS_ABORTED : exit_code;
}

VOID WINAPI ServiceMain(DWORD dwArgc, LPTSTR *lpszArgv) {
  service_status_handle = RegisterServiceCtrlHandlerEx(SERVICE_NAME, HandlerEx, nullptr);
  if (service_status_handle == nullptr) {
    // Nothing we can really do here but terminate ourselves
    ExitProcess(GetLastError());
    return;
  }

  auto log_file_handle = INVALID_HANDLE_VALUE;
  auto ready_event = static_cast<HANDLE>(nullptr);
  std::array<HANDLE, 2> inherited_handles {};

  STARTUPINFOEXW startup_info = {};

  // Tell SCM we're starting
  service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status.dwServiceSpecificExitCode = 0;
  service_status.dwWin32ExitCode = NO_ERROR;
  service_status.dwWaitHint = SERVICE_START_WAIT_HINT;
  service_status.dwControlsAccepted = 0;
  service_status.dwCheckPoint = 0;
  ReportServiceStartPending();

  // Create a manual-reset stop event
  stop_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (stop_event == nullptr) {
    FailServiceStart(startup_info, ready_event, log_file_handle, GetLastError());
    return;
  }

  // Create an auto-reset session change event
  session_change_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (session_change_event == nullptr) {
    FailServiceStart(startup_info, ready_event, log_file_handle, GetLastError());
    return;
  }

  log_file_handle = OpenLogFileHandle();
  if (log_file_handle == INVALID_HANDLE_VALUE) {
    FailServiceStart(startup_info, ready_event, log_file_handle, GetLastError());
    return;
  }

  SECURITY_ATTRIBUTES ready_event_security_attributes = {sizeof(ready_event_security_attributes), nullptr, TRUE};
  ready_event = CreateEventW(&ready_event_security_attributes, TRUE, FALSE, nullptr);
  if (ready_event == nullptr) {
    FailServiceStart(startup_info, ready_event, log_file_handle, GetLastError());
    return;
  }

  // We can use a single STARTUPINFOEXW for all the processes that we launch.
  startup_info.StartupInfo.cb = sizeof(startup_info);
  startup_info.StartupInfo.lpDesktop = (LPWSTR) L"winsta0\\default";
  startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup_info.StartupInfo.hStdInput = nullptr;
  startup_info.StartupInfo.hStdOutput = log_file_handle;
  startup_info.StartupInfo.hStdError = log_file_handle;

  // Allocate an attribute list with space for 2 entries
  startup_info.lpAttributeList = AllocateProcThreadAttributeList(2);
  if (startup_info.lpAttributeList == nullptr) {
    FailServiceStart(startup_info, ready_event, log_file_handle, GetLastError());
    return;
  }

  // Only allow Sunshine.exe to inherit the log file and ready event handles, not all inheritable handles.
  inherited_handles = {log_file_handle, ready_event};
  if (!UpdateProcThreadAttribute(startup_info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, static_cast<void *>(inherited_handles.data()), inherited_handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
    FailServiceStart(startup_info, ready_event, log_file_handle, GetLastError());
    return;
  }

  bool service_running = false;
  bool service_should_stop = false;
  DWORD startup_failures = 0;
  DWORD startup_failure_code = NO_ERROR;
  using enum SunshineReadyResult;

  // Loop every 3 seconds until the stop event is set or Sunshine.exe is running.
  while (!service_should_stop && WaitForSingleObject(stop_event, 3000) != WAIT_OBJECT_0) {
    if (!service_running) {
      ReportServiceStartPending();
    }

    auto console_session_id = WTSGetActiveConsoleSessionId();
    if (console_session_id == 0xFFFFFFFF) {
      // No console session yet
      continue;
    }

    auto console_token = DuplicateTokenForSession(console_session_id);
    if (console_token == nullptr) {
      continue;
    }

    // Job objects cannot span sessions, so we must create one for each process
    auto job_handle = CreateJobObjectForChildProcess();
    if (job_handle == nullptr) {
      CloseHandle(console_token);
      continue;
    }

    // Start Sunshine.exe inside our job object
    if (!UpdateProcThreadAttribute(startup_info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, static_cast<void *>(&job_handle), sizeof(job_handle), nullptr, nullptr)) {
      startup_failure_code = GetLastError();
      CloseHandle(console_token);
      CloseHandle(job_handle);
      service_should_stop = true;
      continue;
    }

    ResetEvent(ready_event);

    if (const auto ready_event_handle_text = std::format("{}", std::bit_cast<std::uintptr_t>(ready_event)); lizardbyte::common::set_env(SERVICE_READY_EVENT_ENV, ready_event_handle_text) != 0) {
      startup_failure_code = ERROR_BAD_ENVIRONMENT;
      CloseHandle(console_token);
      CloseHandle(job_handle);
      service_should_stop = true;
      continue;
    }

    PROCESS_INFORMATION process_info = {};
    const auto process_created = CreateProcessAsUserW(console_token, L"Sunshine.exe", nullptr, nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup_info.StartupInfo, &process_info);
    const auto create_process_error = process_created ? NO_ERROR : GetLastError();
    static_cast<void>(lizardbyte::common::unset_env(SERVICE_READY_EVENT_ENV));
    if (!process_created) {
      startup_failure_code = create_process_error;
      CloseHandle(console_token);
      CloseHandle(job_handle);
      if (++startup_failures >= SUNSHINE_STARTUP_RETRY_LIMIT) {
        service_should_stop = true;
      }
      continue;
    }

    if (auto ready_result = WaitForSunshineReady(process_info.hProcess, ready_event, console_session_id, !service_running); ready_result != ready) {
      if (ready_result == stop || ready_result == session_changed || ready_result == timed_out) {
        TerminateSunshineProcess(console_token, process_info);
      }

      if (ready_result == stop) {
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(console_token);
        CloseHandle(job_handle);
        service_should_stop = true;
        continue;
      }

      if (ready_result == session_changed) {
        startup_failures = 0;
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(console_token);
        CloseHandle(job_handle);
        continue;
      }

      startup_failure_code = ready_result == timed_out ? ERROR_TIMEOUT : GetSunshineStartupFailureCode(process_info.hProcess);

      CloseHandle(process_info.hThread);
      CloseHandle(process_info.hProcess);
      CloseHandle(console_token);
      CloseHandle(job_handle);

      if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) {
        service_should_stop = true;
      }

      if (!service_should_stop) {
        ++startup_failures;
      }

      if (startup_failures >= SUNSHINE_STARTUP_RETRY_LIMIT) {
        service_should_stop = true;
      }
      continue;
    }

    startup_failures = 0;
    startup_failure_code = NO_ERROR;
    if (!service_running) {
      ReportServiceRunning();
      service_running = true;
    }

    bool still_running = false;
    do {
      // Wait for the stop event to be set, Sunshine.exe to terminate, or the console session to change
      const HANDLE wait_objects[] = {stop_event, process_info.hProcess, session_change_event};
      switch (WaitForMultipleObjects(_countof(wait_objects), wait_objects, FALSE, INFINITE)) {
        case WAIT_OBJECT_0 + 2:
          if (WTSGetActiveConsoleSessionId() == console_session_id) {
            // The active console session didn't actually change. Let Sunshine keep running.
            still_running = true;
            continue;
          }
          // Fall-through to terminate Sunshine.exe and start it again.
        case WAIT_OBJECT_0:
          // The service is shutting down, so try to gracefully terminate Sunshine.exe.
          TerminateSunshineProcess(console_token, process_info);
          still_running = false;
          break;

        case WAIT_OBJECT_0 + 1:
          {
            // Sunshine terminated itself.

            DWORD exit_code;
            if (GetExitCodeProcess(process_info.hProcess, &exit_code) && exit_code == ERROR_SHUTDOWN_IN_PROGRESS) {
              // Sunshine is asking for us to shut down, so gracefully stop ourselves.
              SetEvent(stop_event);
            }
            still_running = false;
            break;
          }

        default:
          startup_failure_code = GetLastError();
          service_should_stop = true;
          TerminateSunshineProcess(console_token, process_info);
          still_running = false;
          break;
      }
    } while (still_running);

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    CloseHandle(console_token);
    CloseHandle(job_handle);
  }

  // Let SCM know we've stopped.
  const auto service_exit_code = WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0 ? NO_ERROR : startup_failure_code;
  CleanupServiceResources(startup_info, ready_event, log_file_handle);
  ReportServiceStopped(service_exit_code);
}

// This will run in a child process in the user session
int DoGracefulTermination(DWORD pid) {
  // Attach to Sunshine's console
  if (!AttachConsole(pid)) {
    return GetLastError();
  }

  // Disable our own Ctrl-C handling
  SetConsoleCtrlHandler(nullptr, TRUE);

  // Send a Ctrl-C event to Sunshine
  if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)) {
    return GetLastError();
  }

  return 0;
}

int main(int argc, char *argv[]) {
  static const SERVICE_TABLE_ENTRY service_table[] = {
    {(LPSTR) SERVICE_NAME, ServiceMain},
    {nullptr, nullptr}
  };

  // Check if this is a reinvocation of ourselves to send Ctrl-C to Sunshine.exe
  if (argc == 3 && strcmp(argv[1], "--terminate") == 0) {
    return DoGracefulTermination(atol(argv[2]));
  }

  // By default, services have their current directory set to %SYSTEMROOT%\System32.
  // We want to use the directory where Sunshine.exe is located instead of system32.
  // This requires stripping off 2 path components: the file name and the last folder
  WCHAR module_path[MAX_PATH];
  GetModuleFileNameW(nullptr, module_path, _countof(module_path));
  for (auto i = 0; i < 2; i++) {
    auto last_sep = wcsrchr(module_path, '\\');
    if (last_sep) {
      *last_sep = 0;
    }
  }
  SetCurrentDirectoryW(module_path);

  // Trigger our ServiceMain()
  return StartServiceCtrlDispatcher(service_table);
}
