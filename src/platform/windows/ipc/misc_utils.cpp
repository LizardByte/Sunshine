/**
 * @file misc_utils.cpp
 * @brief Helper functions for Windows platform IPC and process management.
 *
 * This file contains utility functions and RAII helpers to minimize dependencies for the sunshine_wgc_capture tool.
 * Functions include process checks, token management, DACL handling, and secure desktop detection.
 */

// local includes
#include "misc_utils.h"

// platform includes
#include <sddl.h>
#include <tlhelp32.h>
#include <windows.h>
#include <wtsapi32.h>

namespace platf::dxgi {

  // RAII helper for overlapped I/O operations

  io_context::io_context() {
    _event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ZeroMemory(&_ovl, sizeof(_ovl));
    _ovl.hEvent = _event;
  }

  io_context::~io_context() {
    if (_event) {
      CloseHandle(_event);
    }
  }

  io_context::io_context(io_context &&other) noexcept
      :
      _ovl(other._ovl),
      _event(other._event) {
    other._event = nullptr;
    ZeroMemory(&other._ovl, sizeof(other._ovl));
  }

  io_context &io_context::operator=(io_context &&other) noexcept {
    if (this != &other) {
      if (_event) {
        CloseHandle(_event);
      }
      _ovl = other._ovl;
      _event = other._event;
      other._event = nullptr;
      ZeroMemory(&other._ovl, sizeof(other._ovl));
    }
    return *this;
  }

  OVERLAPPED *io_context::get() {
    return &_ovl;
  }

  HANDLE io_context::event() const {
    return _event;
  }

  bool io_context::is_valid() const {
    return _event != nullptr;
  }

  // safe_dacl implementation
  safe_dacl::safe_dacl() = default;

  safe_dacl::safe_dacl(PACL p):
      dacl(p) {}

  safe_dacl::~safe_dacl() {
    if (dacl) {
      LocalFree(dacl);
    }
  }

  safe_dacl::safe_dacl(safe_dacl &&other) noexcept
      :
      dacl(other.dacl) {
    other.dacl = nullptr;
  }

  safe_dacl &safe_dacl::operator=(safe_dacl &&other) noexcept {
    if (this != &other) {
      if (dacl) {
        LocalFree(dacl);
      }
      dacl = other.dacl;
      other.dacl = nullptr;
    }
    return *this;
  }

  void safe_dacl::reset(PACL p) {
    if (dacl) {
      LocalFree(dacl);
    }
    dacl = p;
  }

  PACL safe_dacl::get() const {
    return dacl;
  }

  PACL safe_dacl::release() {
    PACL tmp = dacl;
    dacl = nullptr;
    return tmp;
  }

  safe_dacl::operator bool() const {
    return dacl != nullptr;
  }

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
      if (!GetTokenInformation(userToken, TokenLinkedToken, static_cast<void *>(&linkedToken), sizeof(TOKEN_LINKED_TOKEN), &dwSize)) {
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
      std::wstring desktopName(256, L'\0');
      DWORD needed = 0;
      if (GetUserObjectInformationW(currentDesktop, UOI_NAME, &desktopName[0], static_cast<DWORD>(desktopName.size() * sizeof(wchar_t)), &needed) && (_wcsicmp(desktopName.c_str(), L"Winlogon") == 0 || _wcsicmp(desktopName.c_str(), L"SAD") == 0)) {
        // Secure desktop typically has names like "Winlogon" or "SAD" (Secure Attention Desktop)
        return true;
      }
    }

    return false;
  }

  std::string generate_guid() {
    GUID guid;
    if (CoCreateGuid(&guid) != S_OK) {
      return {};
    }

    std::array<WCHAR, 39> guidStr {};  // "{...}" format, 38 chars + null
    if (StringFromGUID2(guid, guidStr.data(), 39) == 0) {
      return {};
    }

    std::wstring wstr(guidStr.data());
    return wide_to_utf8(wstr);
  }

  std::string wide_to_utf8(const std::wstring &wstr) {
    if (wstr.empty()) {
      return std::string();
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
  }

  std::wstring utf8_to_wide(const std::string &str) {
    if (str.empty()) {
      return std::wstring();
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), nullptr, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), &wstrTo[0], size_needed);
    return wstrTo;
  }

}  // namespace platf::dxgi
