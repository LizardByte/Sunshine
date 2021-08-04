#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wtsapi32.h>

SERVICE_STATUS_HANDLE service_status_handle;
SERVICE_STATUS service_status;
HANDLE stop_event;

#define SERVICE_NAME "SunshineSvc"

DWORD WINAPI HandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
  switch(dwControl) {
  case SERVICE_CONTROL_INTERROGATE:
    return NO_ERROR;

  case SERVICE_CONTROL_STOP:
    // Let SCM know we're stopping in up to 30 seconds
    service_status.dwCurrentState     = SERVICE_STOP_PENDING;
    service_status.dwControlsAccepted = 0;
    service_status.dwWaitHint         = 30 * 1000;
    SetServiceStatus(service_status_handle, &service_status);

    // Trigger ServiceMain() to start cleanup
    SetEvent(stop_event);
    return NO_ERROR;

  default:
    return NO_ERROR;
  }
}

HANDLE DuplicateTokenForConsoleSession() {
  auto console_session_id = WTSGetActiveConsoleSessionId();
  if(console_session_id == 0xFFFFFFFF) {
    // No console session yet
    return NULL;
  }

  HANDLE current_token;
  if(!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &current_token)) {
    return NULL;
  }

  // Duplicate our own LocalSystem token
  HANDLE new_token;
  if(!DuplicateTokenEx(current_token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &new_token)) {
    CloseHandle(current_token);
    return NULL;
  }

  CloseHandle(current_token);

  // Change the duplicated token to the console session ID
  if(!SetTokenInformation(new_token, TokenSessionId, &console_session_id, sizeof(console_session_id))) {
    CloseHandle(new_token);
    return NULL;
  }

  return new_token;
}

HANDLE OpenLogFileHandle() {
  WCHAR log_file_name[MAX_PATH];

  // Create sunshine.log in the Temp folder (usually %SYSTEMROOT%\Temp)
  GetTempPathW(_countof(log_file_name), log_file_name);
  wcscat_s(log_file_name, L"sunshine.log");

  // The file handle must be inheritable for our child process to use it
  SECURITY_ATTRIBUTES security_attributes = { sizeof(security_attributes), NULL, TRUE };

  // Overwrite the old sunshine.log
  return CreateFileW(log_file_name,
                     GENERIC_WRITE,
                     FILE_SHARE_READ,
                     &security_attributes,
                     CREATE_ALWAYS,
                     0,
                     NULL);
}

VOID WINAPI ServiceMain(DWORD dwArgc, LPTSTR *lpszArgv) {
  stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if(stop_event == NULL) {
    return;
  }

  service_status_handle = RegisterServiceCtrlHandlerEx(SERVICE_NAME, HandlerEx, NULL);
  if(service_status_handle == NULL) {
    return;
  }

  auto log_file_handle = OpenLogFileHandle();
  if (log_file_handle == INVALID_HANDLE_VALUE) {
    return;
  }

  // Tell SCM we're running
  service_status.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
  service_status.dwServiceSpecificExitCode = 0;
  service_status.dwWin32ExitCode           = NO_ERROR;
  service_status.dwWaitHint                = 0;
  service_status.dwControlsAccepted        = SERVICE_ACCEPT_STOP;
  service_status.dwCheckPoint              = 0;
  service_status.dwCurrentState            = SERVICE_RUNNING;
  SetServiceStatus(service_status_handle, &service_status);

  // Loop every 3 seconds until the stop event is set or Sunshine.exe is running
  while(WaitForSingleObject(stop_event, 3000) != WAIT_OBJECT_0) {
    auto console_token = DuplicateTokenForConsoleSession();
    if(console_token == NULL) {
      continue;
    }

    STARTUPINFOW startup_info = {};
    startup_info.cb         = sizeof(startup_info);
    startup_info.lpDesktop  = (LPWSTR)L"winsta0\\default";
    startup_info.dwFlags    = STARTF_USESTDHANDLES;
    startup_info.hStdInput  = INVALID_HANDLE_VALUE;
    startup_info.hStdOutput = log_file_handle;
    startup_info.hStdError  = log_file_handle;

    PROCESS_INFORMATION process_info;
    if(!CreateProcessAsUserW(console_token,
                             L"Sunshine.exe",
                             NULL,
                             NULL,
                             NULL,
                             TRUE,
                             ABOVE_NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                             NULL,
                             NULL,
                             &startup_info,
                             &process_info)) {
      CloseHandle(console_token);
      continue;
    }

    // Close handles that are no longer needed
    CloseHandle(console_token);
    CloseHandle(process_info.hThread);

    // Wait for either the stop event to be set or Sunshine.exe to terminate
    const HANDLE wait_objects[] = { stop_event, process_info.hProcess };
    switch(WaitForMultipleObjects(_countof(wait_objects), wait_objects, FALSE, INFINITE)) {
    case WAIT_OBJECT_0:
      // The service is shutting down, so terminate Sunshine.exe.
      // TODO: Send a graceful exit request and only terminate forcefully as a last resort.
      TerminateProcess(process_info.hProcess, ERROR_PROCESS_ABORTED);
      break;

    case WAIT_OBJECT_0 + 1:
      // Sunshine terminated itself.
      break;
    }

    CloseHandle(process_info.hProcess);
  }

  // Let SCM know we've stopped
  service_status.dwCurrentState = SERVICE_STOPPED;
  SetServiceStatus(service_status_handle, &service_status);
}

int main(int argc, char* argv[])
{
  static const SERVICE_TABLE_ENTRY service_table[] = {
    { (LPSTR)SERVICE_NAME, ServiceMain },
    { NULL, NULL }
  };

  // By default, services have their current directory set to %SYSTEMROOT%\System32.
  // We want to use the directory where Sunshine.exe is located instead of system32.
  // This requires stripping off 2 path components: the file name and the last folder
  WCHAR module_path[MAX_PATH];
  GetModuleFileNameW(NULL, module_path, _countof(module_path));
  for(auto i = 0; i < 2; i++) {
    auto last_sep = wcsrchr(module_path, '\\');
    if(last_sep) {
      *last_sep = 0;
    }
  }
  SetCurrentDirectoryW(module_path);

  // Trigger our ServiceMain()
  return StartServiceCtrlDispatcher(service_table);
}
