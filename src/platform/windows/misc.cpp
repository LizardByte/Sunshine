#include <filesystem>
#include <iomanip>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/process.hpp>

// prevent clang format from "optimizing" the header include order
// clang-format off
#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#include <winuser.h>
#include <ws2tcpip.h>
#include <userenv.h>
// clang-format on

#include "src/main.h"
#include "src/utility.h"

namespace bp = boost::process;

using namespace std::literals;
namespace platf {
using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

std::filesystem::path appdata() {
  return L"."sv;
}

std::string from_sockaddr(const sockaddr *const socket_address) {
  char data[INET6_ADDRSTRLEN];

  auto family = socket_address->sa_family;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6 *)socket_address)->sin6_addr, data, INET6_ADDRSTRLEN);
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in *)socket_address)->sin_addr, data, INET_ADDRSTRLEN);
  }

  return std::string { data };
}

std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const ip_addr) {
  char data[INET6_ADDRSTRLEN];

  auto family = ip_addr->sa_family;
  std::uint16_t port;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6 *)ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
    port = ((sockaddr_in6 *)ip_addr)->sin6_port;
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in *)ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
    port = ((sockaddr_in *)ip_addr)->sin_port;
  }

  return { port, std::string { data } };
}

adapteraddrs_t get_adapteraddrs() {
  adapteraddrs_t info { nullptr };
  ULONG size = 0;

  while(GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, info.get(), &size) == ERROR_BUFFER_OVERFLOW) {
    info.reset((PIP_ADAPTER_ADDRESSES)malloc(size));
  }

  return info;
}

std::string get_mac_address(const std::string_view &address) {
  adapteraddrs_t info = get_adapteraddrs();
  for(auto adapter_pos = info.get(); adapter_pos != nullptr; adapter_pos = adapter_pos->Next) {
    for(auto addr_pos = adapter_pos->FirstUnicastAddress; addr_pos != nullptr; addr_pos = addr_pos->Next) {
      if(adapter_pos->PhysicalAddressLength != 0 && address == from_sockaddr(addr_pos->Address.lpSockaddr)) {
        std::stringstream mac_addr;
        mac_addr << std::hex;
        for(int i = 0; i < adapter_pos->PhysicalAddressLength; i++) {
          if(i > 0) {
            mac_addr << ':';
          }
          mac_addr << std::setw(2) << std::setfill('0') << (int)adapter_pos->PhysicalAddress[i];
        }
        return mac_addr.str();
      }
    }
  }
  BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
  return "00:00:00:00:00:00"s;
}

HDESK syncThreadDesktop() {
  auto hDesk = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
  if(!hDesk) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to Open Input Desktop [0x"sv << util::hex(err).to_string_view() << ']';

    return nullptr;
  }

  if(!SetThreadDesktop(hDesk)) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to sync desktop to thread [0x"sv << util::hex(err).to_string_view() << ']';
  }

  CloseDesktop(hDesk);

  return hDesk;
}

void print_status(const std::string_view &prefix, HRESULT status) {
  char err_string[1024];

  DWORD bytes = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr,
    status,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    err_string,
    sizeof(err_string),
    nullptr);

  BOOST_LOG(error) << prefix << ": "sv << std::string_view { err_string, bytes };
}

std::wstring utf8_to_wide_string(const std::string &str) {
  // Determine the size required for the destination string
  int chars = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), NULL, 0);

  // Allocate it
  wchar_t buffer[chars] = {};

  // Do the conversion for real
  chars = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), buffer, chars);
  return std::wstring(buffer, chars);
}

std::string wide_to_utf8_string(const std::wstring &str) {
  // Determine the size required for the destination string
  int bytes = WideCharToMultiByte(CP_UTF8, 0, str.data(), str.length(), NULL, 0, NULL, NULL);

  // Allocate it
  char buffer[bytes] = {};

  // Do the conversion for real
  bytes = WideCharToMultiByte(CP_UTF8, 0, str.data(), str.length(), buffer, bytes, NULL, NULL);
  return std::string(buffer, bytes);
}

HANDLE duplicate_shell_token() {
  // Get the shell window (will usually be owned by explorer.exe)
  HWND shell_window = GetShellWindow();
  if(!shell_window) {
    BOOST_LOG(error) << "No shell window found. Is explorer.exe running?"sv;
    return NULL;
  }

  // Open a handle to the explorer.exe process
  DWORD shell_pid;
  GetWindowThreadProcessId(shell_window, &shell_pid);
  HANDLE shell_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
  if(!shell_process) {
    BOOST_LOG(error) << "Failed to open shell process: "sv << GetLastError();
    return NULL;
  }

  // Open explorer's token to clone for process creation
  HANDLE shell_token;
  BOOL ret = OpenProcessToken(shell_process, TOKEN_DUPLICATE, &shell_token);
  CloseHandle(shell_process);
  if(!ret) {
    BOOST_LOG(error) << "Failed to open shell process token: "sv << GetLastError();
    return NULL;
  }

  // Duplicate the token to make it usable for process creation
  HANDLE new_token;
  ret = DuplicateTokenEx(shell_token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &new_token);
  CloseHandle(shell_token);
  if(!ret) {
    BOOST_LOG(error) << "Failed to duplicate shell process token: "sv << GetLastError();
    return NULL;
  }

  return new_token;
}

PTOKEN_USER get_token_user(HANDLE token) {
  DWORD return_length;
  if(GetTokenInformation(token, TokenUser, NULL, 0, &return_length) || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    auto winerr = GetLastError();
    BOOST_LOG(error) << "Failed to get token information size: "sv << winerr;
    return nullptr;
  }

  auto user = (PTOKEN_USER)HeapAlloc(GetProcessHeap(), 0, return_length);
  if(!user) {
    return nullptr;
  }

  if(!GetTokenInformation(token, TokenUser, user, return_length, &return_length)) {
    auto winerr = GetLastError();
    BOOST_LOG(error) << "Failed to get token information: "sv << winerr;
    HeapFree(GetProcessHeap(), 0, user);
    return nullptr;
  }

  return user;
}

void free_token_user(PTOKEN_USER user) {
  HeapFree(GetProcessHeap(), 0, user);
}

bool is_token_same_user_as_process(HANDLE other_token) {
  HANDLE process_token;
  if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token)) {
    auto winerr = GetLastError();
    BOOST_LOG(error) << "Failed to open process token: "sv << winerr;
    return false;
  }

  auto process_user = get_token_user(process_token);
  CloseHandle(process_token);
  if(!process_user) {
    return false;
  }

  auto token_user = get_token_user(other_token);
  if(!token_user) {
    free_token_user(process_user);
    return false;
  }

  bool ret = EqualSid(process_user->User.Sid, token_user->User.Sid);

  free_token_user(process_user);
  free_token_user(token_user);

  return ret;
}

bool merge_user_environment_block(bp::environment &env, HANDLE shell_token) {
  // Get the target user's environment block
  PVOID env_block;
  if(!CreateEnvironmentBlock(&env_block, shell_token, FALSE)) {
    return false;
  }

  // Parse the environment block and populate env
  for(auto c = (PWCHAR)env_block; *c != UNICODE_NULL; c += wcslen(c) + 1) {
    // Environment variable entries end with a null-terminator, so std::wstring() will get an entire entry.
    std::string env_tuple = wide_to_utf8_string(std::wstring { c });
    std::string env_name  = env_tuple.substr(0, env_tuple.find('='));
    std::string env_val   = env_tuple.substr(env_tuple.find('=') + 1);

    // Perform a case-insensitive search to see if this variable name already exists
    auto itr = std::find_if(env.cbegin(), env.cend(),
      [&](const auto &e) { return boost::iequals(e.get_name(), env_name); });
    if(itr != env.cend()) {
      // Use this existing name if it is already present to ensure we merge properly
      env_name = itr->get_name();
    }

    // For the PATH variable, we will merge the values together
    if(boost::iequals(env_name, "PATH")) {
      env[env_name] = env_val + ";" + env[env_name].to_string();
    }
    else {
      // Other variables will be superseded by those in the user's environment block
      env[env_name] = env_val;
    }
  }

  DestroyEnvironmentBlock(env_block);
  return true;
}

// Note: This does NOT append a null terminator
void append_string_to_environment_block(wchar_t *env_block, int &offset, const std::wstring &wstr) {
  std::memcpy(&env_block[offset], wstr.data(), wstr.length() * sizeof(wchar_t));
  offset += wstr.length();
}

std::wstring create_environment_block(bp::environment &env) {
  int size = 0;
  for(const auto &entry : env) {
    auto name  = entry.get_name();
    auto value = entry.to_string();
    size += utf8_to_wide_string(name).length() + 1 /* L'=' */ + utf8_to_wide_string(value).length() + 1 /* L'\0' */;
  }

  size += 1 /* L'\0' */;

  wchar_t env_block[size];
  int offset = 0;
  for(const auto &entry : env) {
    auto name  = entry.get_name();
    auto value = entry.to_string();

    // Construct the NAME=VAL\0 string
    append_string_to_environment_block(env_block, offset, utf8_to_wide_string(name));
    env_block[offset++] = L'=';
    append_string_to_environment_block(env_block, offset, utf8_to_wide_string(value));
    env_block[offset++] = L'\0';
  }

  // Append a final null terminator
  env_block[offset++] = L'\0';

  return std::wstring(env_block, offset);
}

LPPROC_THREAD_ATTRIBUTE_LIST allocate_proc_thread_attr_list(DWORD attribute_count) {
  SIZE_T size;
  InitializeProcThreadAttributeList(NULL, attribute_count, 0, &size);

  auto list = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
  if(list == NULL) {
    return NULL;
  }

  if(!InitializeProcThreadAttributeList(list, attribute_count, 0, &size)) {
    HeapFree(GetProcessHeap(), 0, list);
    return NULL;
  }

  return list;
}

void free_proc_thread_attr_list(LPPROC_THREAD_ATTRIBUTE_LIST list) {
  DeleteProcThreadAttributeList(list);
  HeapFree(GetProcessHeap(), 0, list);
}

bp::child run_unprivileged(const std::string &cmd, boost::filesystem::path &working_dir, bp::environment &env, FILE *file, std::error_code &ec) {
  HANDLE shell_token = duplicate_shell_token();
  if(!shell_token) {
    // This can happen if the shell has crashed. Fail the launch rather than risking launching with
    // Sunshine's permissions unmodified.
    ec = std::make_error_code(std::errc::no_such_process);
    return bp::child();
  }

  auto token_close = util::fail_guard([shell_token]() {
    CloseHandle(shell_token);
  });

  // Populate env with user-specific environment variables
  if(!merge_user_environment_block(env, shell_token)) {
    ec = std::make_error_code(std::errc::not_enough_memory);
    return bp::child();
  }

  // Most Win32 APIs can't consume UTF-8 strings directly, so we must convert them into UTF-16
  std::wstring wcmd      = utf8_to_wide_string(cmd);
  std::wstring env_block = create_environment_block(env);
  std::wstring start_dir = utf8_to_wide_string(working_dir.string());

  STARTUPINFOEXW startup_info = {};
  startup_info.StartupInfo.cb = sizeof(startup_info);

  // Allocate a process attribute list with space for 1 element
  startup_info.lpAttributeList = allocate_proc_thread_attr_list(1);
  if(startup_info.lpAttributeList == NULL) {
    ec = std::make_error_code(std::errc::not_enough_memory);
    return bp::child();
  }

  auto attr_list_free = util::fail_guard([list = startup_info.lpAttributeList]() {
    free_proc_thread_attr_list(list);
  });

  if(file) {
    HANDLE log_file_handle = (HANDLE)_get_osfhandle(_fileno(file));

    // Populate std handles if the caller gave us a log file to use
    startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startup_info.StartupInfo.hStdInput  = NULL;
    startup_info.StartupInfo.hStdOutput = log_file_handle;
    startup_info.StartupInfo.hStdError  = log_file_handle;

    // Allow the log file handle to be inherited by the child process (without inheriting all of
    // our inheritable handles, such as our own log file handle created by SunshineSvc).
    UpdateProcThreadAttribute(startup_info.lpAttributeList,
      0,
      PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
      &log_file_handle,
      sizeof(log_file_handle),
      NULL,
      NULL);
  }

  // If we're running with the same user account as the shell, just use CreateProcess().
  // This will launch the child process elevated if Sunshine is elevated.
  PROCESS_INFORMATION process_info;
  BOOL ret;
  if(!is_token_same_user_as_process(shell_token)) {
    // Impersonate the user when launching the process. This will ensure that appropriate access
    // checks are done against the user token, not our SYSTEM token. It will also allow network
    // shares and mapped network drives to be used as launch targets, since those credentials
    // are stored per-user.
    if(!ImpersonateLoggedOnUser(shell_token)) {
      auto winerror = GetLastError();
      BOOST_LOG(error) << "Failed to impersonate user: "sv << winerror;
      ec = std::make_error_code(std::errc::permission_denied);
      return bp::child();
    }

    // Launch the process with the duplicated shell token.
    // Set CREATE_BREAKAWAY_FROM_JOB to avoid the child being killed if SunshineSvc.exe is terminated.
    // Set CREATE_NEW_CONSOLE to avoid writing stdout to Sunshine's log if 'file' is not specified.
    ret = CreateProcessAsUserW(shell_token,
      NULL,
      (LPWSTR)wcmd.c_str(),
      NULL,
      NULL,
      !!(startup_info.StartupInfo.dwFlags & STARTF_USESTDHANDLES),
      EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB,
      env_block.data(),
      start_dir.empty() ? NULL : start_dir.c_str(),
      (LPSTARTUPINFOW)&startup_info,
      &process_info);

    // End impersonation of the logged on user. If this fails (which is extremely unlikely),
    // we will be running with an unknown user token. The only safe thing to do in that case
    // is terminate ourselves.
    if(!RevertToSelf()) {
      auto winerror = GetLastError();
      BOOST_LOG(fatal) << "Failed to revert to self after impersonation: "sv << winerror;
      std::abort();
    }
  }
  else {
    ret = CreateProcessW(NULL,
      (LPWSTR)wcmd.c_str(),
      NULL,
      NULL,
      !!(startup_info.StartupInfo.dwFlags & STARTF_USESTDHANDLES),
      EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB,
      env_block.data(),
      start_dir.empty() ? NULL : start_dir.c_str(),
      (LPSTARTUPINFOW)&startup_info,
      &process_info);
  }

  if(ret) {
    // Since we are always spawning a process with a less privileged token than ourselves,
    // bp::child() should have no problem opening it with any access rights it wants.
    auto child = bp::child((bp::pid_t)process_info.dwProcessId);

    // Only close handles after bp::child() has opened the process. If the process terminates
    // quickly, the PID could be reused if we close the process handle.
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    BOOST_LOG(info) << cmd << " running with PID "sv << child.id();
    return child;
  }
  else {
    // We must NOT try bp::child() here, since this case can potentially be induced by ACL
    // manipulation (denying yourself execute permission) to cause an escalation of privilege.
    auto winerror = GetLastError();
    BOOST_LOG(error) << "Failed to launch process: "sv << winerror;
    ec = std::make_error_code(std::errc::invalid_argument);
    return bp::child();
  }
}

} // namespace platf