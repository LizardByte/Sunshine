/**
 * @file src/platform/windows/ipc/misc_utils.cpp
 * @brief Helper functions for Windows platform IPC and process management.
 */
// local includes
#include "misc_utils.h"

// platform includes
#include <sddl.h>
#include <tlhelp32.h>
#include <windows.h>
#include <wtsapi32.h>

namespace platf::dxgi {

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

  safe_dacl::safe_dacl(PACL dacl_ptr):
      _dacl(dacl_ptr) {}

  safe_dacl::~safe_dacl() {
    if (_dacl) {
      LocalFree(_dacl);
    }
  }

  safe_dacl::safe_dacl(safe_dacl &&other) noexcept
      :
      _dacl(other._dacl) {
    other._dacl = nullptr;
  }

  safe_dacl &safe_dacl::operator=(safe_dacl &&other) noexcept {
    if (this != &other) {
      if (_dacl) {
        LocalFree(_dacl);
      }
      _dacl = other._dacl;
      other._dacl = nullptr;
    }
    return *this;
  }

  void safe_dacl::reset(PACL dacl_ptr) {
    if (_dacl) {
      LocalFree(_dacl);
    }
    _dacl = dacl_ptr;
  }

  PACL safe_dacl::get() const {
    return _dacl;
  }

  PACL safe_dacl::release() {
    PACL tmp = _dacl;
    _dacl = nullptr;
    return tmp;
  }

  safe_dacl::operator bool() const {
    return _dacl != nullptr;
  }

  bool is_user_admin(HANDLE user_token) {
    WINBOOL is_admin;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID administrators_group;
    is_admin = AllocateAndInitializeSid(
      &nt_authority,
      2,
      SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_ADMINS,
      0,
      0,
      0,
      0,
      0,
      0,
      &administrators_group
    );
    if (is_admin) {
      if (!CheckTokenMembership(user_token, administrators_group, &is_admin)) {
        is_admin = false;
      }
      FreeSid(administrators_group);
    }

    return is_admin;
  }

  bool is_running_as_system() {
    BOOL is_system;
    PSID system_sid;
    DWORD sid_size = SECURITY_MAX_SID_SIZE;

    // Allocate memory for the SID structure
    system_sid = LocalAlloc(LMEM_FIXED, sid_size);
    if (system_sid == nullptr) {
      return false;
    }

    // Create a SID for the local system account
    is_system = CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid, &sid_size);
    if (is_system && !CheckTokenMembership(nullptr, system_sid, &is_system)) {
      is_system = false;
    }

    // Free the memory allocated for the SID structure
    LocalFree(system_sid);
    return is_system;
  }

  HANDLE retrieve_users_token(bool elevated) {
    DWORD console_session_id;
    HANDLE user_token;
    TOKEN_ELEVATION_TYPE elevation_type;
    DWORD size;

    // Get the session ID of the active console session
    console_session_id = WTSGetActiveConsoleSessionId();
    if (0xFFFFFFFF == console_session_id) {
      return nullptr;
    }

    // Get the user token for the active console session
    if (!WTSQueryUserToken(console_session_id, &user_token)) {
      return nullptr;
    }

    // We need to know if this is an elevated token or not.
    // Get the elevation type of the user token
    if (!GetTokenInformation(user_token, TokenElevationType, &elevation_type, sizeof(TOKEN_ELEVATION_TYPE), &size)) {
      CloseHandle(user_token);
      return nullptr;
    }

    // User is currently not an administrator
    if (elevated && (elevation_type == TokenElevationTypeDefault && !is_user_admin(user_token))) {
      // Don't elevate, just return the token as is
    }

    // User has a limited token, this means they have UAC enabled and is an Administrator
    if (elevated && elevation_type == TokenElevationTypeLimited) {
      TOKEN_LINKED_TOKEN linked_token;
      // Retrieve the administrator token that is linked to the limited token
      if (!GetTokenInformation(user_token, TokenLinkedToken, static_cast<void *>(&linked_token), sizeof(TOKEN_LINKED_TOKEN), &size)) {
        CloseHandle(user_token);
        return nullptr;
      }

      // Since we need the elevated token, we'll replace it with their administrative token.
      CloseHandle(user_token);
      user_token = linked_token.LinkedToken;
    }

    // We don't need to do anything for TokenElevationTypeFull users here, because they're already elevated.
    return user_token;
  }

  bool is_process_running(const std::wstring &process_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
      return false;
    }

    PROCESSENTRY32W process_entry = {};
    process_entry.dwSize = sizeof(process_entry);

    bool found = false;
    if (Process32FirstW(snapshot, &process_entry)) {
      do {
        if (_wcsicmp(process_entry.szExeFile, process_name.c_str()) == 0) {
          found = true;
          break;
        }
      } while (Process32NextW(snapshot, &process_entry));
    }

    CloseHandle(snapshot);
    return found;
  }

  bool is_secure_desktop_active() {
    // Open the input desktop for the current session
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (!hDesk) {
      return false;  // can't open — treat as not secure
    }

    bool isSecure = false;

    // 1. Check the desktop name
    wchar_t name[256];
    DWORD needed = 0;
    if (GetUserObjectInformationW(hDesk, UOI_NAME, name, sizeof(name), &needed)) {
      if (_wcsicmp(name, L"Winlogon") == 0 || _wcsicmp(name, L"SAD") == 0) {
        isSecure = true;
      }
    }

    // 2. Check the desktop's owning SID (extra confirmation)
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE];
    if (GetUserObjectInformationW(hDesk, UOI_USER_SID, sidBuffer, sizeof(sidBuffer), &needed)) {
      PSID sid = reinterpret_cast<PSID>(sidBuffer);
      LPWSTR sidStr = nullptr;
      if (ConvertSidToStringSidW(sid, &sidStr)) {
        if (_wcsicmp(sidStr, L"S-1-5-18") == 0) {  // LocalSystem SID
          isSecure = true;
        }
        LocalFree(sidStr);
      }
    }

    CloseDesktop(hDesk);
    return isSecure;
  }

  std::string generate_guid() {
    GUID guid;
    if (CoCreateGuid(&guid) != S_OK) {
      return {};
    }

    std::array<WCHAR, 39> guid_str {};  // "{...}" format, 38 chars + null
    if (StringFromGUID2(guid, guid_str.data(), 39) == 0) {
      return {};
    }

    std::wstring wstr(guid_str.data());
    return wide_to_utf8(wstr);
  }

  std::string wide_to_utf8(const std::wstring &wide_str) {
    if (wide_str.empty()) {
      return std::string();
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wide_str[0], (int) wide_str.size(), nullptr, 0, nullptr, nullptr);
    std::string str_to(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wide_str[0], (int) wide_str.size(), &str_to[0], size_needed, nullptr, nullptr);
    return str_to;
  }

  std::wstring utf8_to_wide(const std::string &utf8_str) {
    if (utf8_str.empty()) {
      return std::wstring();
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int) utf8_str.size(), nullptr, 0);
    std::wstring wstr_to(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int) utf8_str.size(), &wstr_to[0], size_needed);
    return wstr_to;
  }

}  // namespace platf::dxgi
