/**
 * @file src/platform/windows/wgc/misc_utils.cpp
 * @brief Minimal utility functions for WGC helper without heavy dependencies
 */

#include "misc_utils.h"

#include <sddl.h>
#include <tlhelp32.h>
#include <windows.h>
#include <wtsapi32.h>

namespace platf::wgc {

  bool IsUserAdmin(HANDLE user_token) {
    WINBOOL ret;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;
    ret = AllocateAndInitializeSid(
      &NtAuthority,
      2,
      SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_ADMINS,
      0,
      0,
      0,
      0,
      0,
      0,
      &AdministratorsGroup
    );
    if (ret) {
      if (!CheckTokenMembership(user_token, AdministratorsGroup, &ret)) {
        ret = false;
      }
      FreeSid(AdministratorsGroup);
    }

    return ret;
  }

  /**
   * @brief Check if the current process is running with system-level privileges.
   * @return `true` if the current process has system-level privileges, `false` otherwise.
   */
  bool is_running_as_system() {
    BOOL ret;
    PSID SystemSid;
    DWORD dwSize = SECURITY_MAX_SID_SIZE;

    // Allocate memory for the SID structure
    SystemSid = LocalAlloc(LMEM_FIXED, dwSize);
    if (SystemSid == nullptr) {
      return false;
    }

    // Create a SID for the local system account
    ret = CreateWellKnownSid(WinLocalSystemSid, nullptr, SystemSid, &dwSize);
    if (ret && !CheckTokenMembership(nullptr, SystemSid, &ret)) {
      ret = false;
    }

    // Free the memory allocated for the SID structure
    LocalFree(SystemSid);
    return ret;
  }

  /**
   * @brief Obtain the current sessions user's primary token with elevated privileges.
   * @return The user's token. If user has admin capability it will be elevated, otherwise it will be a limited token. On error, `nullptr`.
   */
  HANDLE retrieve_users_token(bool elevated) {
    DWORD consoleSessionId;
    HANDLE userToken;
    TOKEN_ELEVATION_TYPE elevationType;
    DWORD dwSize;

    // Get the session ID of the active console session
    consoleSessionId = WTSGetActiveConsoleSessionId();
    if (0xFFFFFFFF == consoleSessionId) {
      return nullptr;
    }

    // Get the user token for the active console session
    if (!WTSQueryUserToken(consoleSessionId, &userToken)) {
      return nullptr;
    }

    // We need to know if this is an elevated token or not.
    // Get the elevation type of the user token
    if (!GetTokenInformation(userToken, TokenElevationType, &elevationType, sizeof(TOKEN_ELEVATION_TYPE), &dwSize)) {
      CloseHandle(userToken);
      return nullptr;
    }

    // User is currently not an administrator
    if (elevated && (elevationType == TokenElevationTypeDefault && !IsUserAdmin(userToken))) {
      // Don't elevate, just return the token as is
    }

    // User has a limited token, this means they have UAC enabled and is an Administrator
    if (elevated && elevationType == TokenElevationTypeLimited) {
      TOKEN_LINKED_TOKEN linkedToken;
      // Retrieve the administrator token that is linked to the limited token
      if (!GetTokenInformation(userToken, TokenLinkedToken, reinterpret_cast<void *>(&linkedToken), sizeof(TOKEN_LINKED_TOKEN), &dwSize)) {
        CloseHandle(userToken);
        return nullptr;
      }

      // Since we need the elevated token, we'll replace it with their administrative token.
      CloseHandle(userToken);
      userToken = linkedToken.LinkedToken;
    }

    // We don't need to do anything for TokenElevationTypeFull users here, because they're already elevated.
    return userToken;
  }

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
    if (HDESK currentDesktop = GetThreadDesktop(GetCurrentThreadId())) {
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

  DWORD get_parent_process_id(DWORD process_id) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
      return 0;
    }

    PROCESSENTRY32W processEntry = {};
    processEntry.dwSize = sizeof(processEntry);

    DWORD parent_pid = 0;
    if (Process32FirstW(snapshot, &processEntry)) {
      do {
        if (processEntry.th32ProcessID == process_id) {
          parent_pid = processEntry.th32ParentProcessID;
          break;
        }
      } while (Process32NextW(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return parent_pid;
  }

  DWORD get_parent_process_id() {
    return get_parent_process_id(GetCurrentProcessId());
  }

}  // namespace platf::wgc
