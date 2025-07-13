#include <string>
#include <windows.h>
#include <tlhelp32.h>
namespace platf {
  // Function to check if a process with the given name is running
  bool is_process_running(const std::wstring &processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
      return false;
    }

    PROCESSENTRY32W processEntry = {};
    processEntry.dwSize = sizeof(processEntry);

    bool found = false;
    if (Process32FirstW(snapshot, &processEntry)) {
      do {
        if (_wcsicmp(processEntry.szExeFile, processName.c_str()) == 0) {
          found = true;
          break;
        }
      } while (Process32NextW(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return found;
  }

  // Function to check if we're on the secure desktop
  bool is_secure_desktop_active() {
    // Check for UAC (consent.exe)
    if (is_process_running(L"consent.exe")) {
      return true;
    }

    // Check for login screen by looking for winlogon.exe with specific conditions
    // or check the current desktop name
    HDESK currentDesktop = GetThreadDesktop(GetCurrentThreadId());
    if (currentDesktop) {
      wchar_t desktopName[256] = {0};
      DWORD needed = 0;
      if (GetUserObjectInformationW(currentDesktop, UOI_NAME, desktopName, sizeof(desktopName), &needed)) {
        // Secure desktop typically has names like "Winlogon" or "SAD" (Secure Attention Desktop)
        if (_wcsicmp(desktopName, L"Winlogon") == 0 || _wcsicmp(desktopName, L"SAD") == 0) {
          return true;
        }
      }
    }

    return false;
  }
}  // namespace platf
