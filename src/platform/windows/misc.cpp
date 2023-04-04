#include <csignal>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/process.hpp>

// prevent clang format from "optimizing" the header include order
// clang-format off
#include <dwmapi.h>
#include <iphlpapi.h>
#include <iterator>
#include <timeapi.h>
#include <userenv.h>
#include <winsock2.h>
#include <windows.h>
#include <winuser.h>
#include <wlanapi.h>
#include <ws2tcpip.h>
#include <wtsapi32.h>
#include <sddl.h>
// clang-format on

#include "src/main.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include <iterator>

// UDP_SEND_MSG_SIZE was added in the Windows 10 20H1 SDK
#ifndef UDP_SEND_MSG_SIZE
  #define UDP_SEND_MSG_SIZE 2
#endif

// MinGW headers are missing qWAVE stuff
typedef UINT32 QOS_FLOWID, *PQOS_FLOWID;
#define QOS_NON_ADAPTIVE_FLOW 0x00000002
#include <qos2.h>

#ifndef WLAN_API_MAKE_VERSION
  #define WLAN_API_MAKE_VERSION(_major, _minor) (((DWORD) (_minor)) << 16 | (_major))
#endif

namespace bp = boost::process;

using namespace std::literals;
namespace platf {
  using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

  bool enabled_mouse_keys = false;
  MOUSEKEYS previous_mouse_keys_state;

  HANDLE qos_handle = nullptr;

  decltype(QOSCreateHandle) *fn_QOSCreateHandle = nullptr;
  decltype(QOSAddSocketToFlow) *fn_QOSAddSocketToFlow = nullptr;
  decltype(QOSRemoveSocketFromFlow) *fn_QOSRemoveSocketFromFlow = nullptr;

  HANDLE wlan_handle = nullptr;

  decltype(WlanOpenHandle) *fn_WlanOpenHandle = nullptr;
  decltype(WlanCloseHandle) *fn_WlanCloseHandle = nullptr;
  decltype(WlanFreeMemory) *fn_WlanFreeMemory = nullptr;
  decltype(WlanEnumInterfaces) *fn_WlanEnumInterfaces = nullptr;
  decltype(WlanSetInterface) *fn_WlanSetInterface = nullptr;

  std::filesystem::path
  appdata() {
    WCHAR sunshine_path[MAX_PATH];
    GetModuleFileNameW(NULL, sunshine_path, _countof(sunshine_path));
    return std::filesystem::path { sunshine_path }.remove_filename() / L"config"sv;
  }

  std::string
  from_sockaddr(const sockaddr *const socket_address) {
    char data[INET6_ADDRSTRLEN];

    auto family = socket_address->sa_family;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) socket_address)->sin6_addr, data, INET6_ADDRSTRLEN);
    }

    if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) socket_address)->sin_addr, data, INET_ADDRSTRLEN);
    }

    return std::string { data };
  }

  std::pair<std::uint16_t, std::string>
  from_sockaddr_ex(const sockaddr *const ip_addr) {
    char data[INET6_ADDRSTRLEN];

    auto family = ip_addr->sa_family;
    std::uint16_t port;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
      port = ((sockaddr_in6 *) ip_addr)->sin6_port;
    }

    if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
      port = ((sockaddr_in *) ip_addr)->sin_port;
    }

    return { port, std::string { data } };
  }

  adapteraddrs_t
  get_adapteraddrs() {
    adapteraddrs_t info { nullptr };
    ULONG size = 0;

    while (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, info.get(), &size) == ERROR_BUFFER_OVERFLOW) {
      info.reset((PIP_ADAPTER_ADDRESSES) malloc(size));
    }

    return info;
  }

  std::string
  get_mac_address(const std::string_view &address) {
    adapteraddrs_t info = get_adapteraddrs();
    for (auto adapter_pos = info.get(); adapter_pos != nullptr; adapter_pos = adapter_pos->Next) {
      for (auto addr_pos = adapter_pos->FirstUnicastAddress; addr_pos != nullptr; addr_pos = addr_pos->Next) {
        if (adapter_pos->PhysicalAddressLength != 0 && address == from_sockaddr(addr_pos->Address.lpSockaddr)) {
          std::stringstream mac_addr;
          mac_addr << std::hex;
          for (int i = 0; i < adapter_pos->PhysicalAddressLength; i++) {
            if (i > 0) {
              mac_addr << ':';
            }
            mac_addr << std::setw(2) << std::setfill('0') << (int) adapter_pos->PhysicalAddress[i];
          }
          return mac_addr.str();
        }
      }
    }
    BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
    return "00:00:00:00:00:00"s;
  }

  HDESK
  syncThreadDesktop() {
    auto hDesk = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
    if (!hDesk) {
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to Open Input Desktop [0x"sv << util::hex(err).to_string_view() << ']';

      return nullptr;
    }

    if (!SetThreadDesktop(hDesk)) {
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to sync desktop to thread [0x"sv << util::hex(err).to_string_view() << ']';
    }

    CloseDesktop(hDesk);

    return hDesk;
  }

  void
  print_status(const std::string_view &prefix, HRESULT status) {
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

  std::wstring
  utf8_to_wide_string(const std::string &str) {
    // Determine the size required for the destination string
    int chars = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), NULL, 0);

    // Allocate it
    wchar_t buffer[chars] = {};

    // Do the conversion for real
    chars = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), buffer, chars);
    return std::wstring(buffer, chars);
  }

  std::string
  wide_to_utf8_string(const std::wstring &str) {
    // Determine the size required for the destination string
    int bytes = WideCharToMultiByte(CP_UTF8, 0, str.data(), str.length(), NULL, 0, NULL, NULL);

    // Allocate it
    char buffer[bytes] = {};

    // Do the conversion for real
    bytes = WideCharToMultiByte(CP_UTF8, 0, str.data(), str.length(), buffer, bytes, NULL, NULL);
    return std::string(buffer, bytes);
  }

  /**
 * @brief A function to duplicate the current sessions user's token with elevated privileges
 *
 * @return A handle to the duplicated users token, or null if the duplication failed
 */
HANDLE
  duplicate_users_token_elevated() {
    DWORD consoleSessionId;
    HANDLE userToken, duplicateToken;
    TOKEN_ELEVATION_TYPE elevationType;
    DWORD dwSize;

    // Close the userToken handle when it goes out of scope
    auto token_close = util::fail_guard([&userToken]() {
      CloseHandle(userToken);
    });

    // Get the session ID of the active console session
    consoleSessionId = WTSGetActiveConsoleSessionId();
    if (0xFFFFFFFF == consoleSessionId) {
      // If there is no active console session, log a warning and return null
      BOOST_LOG(warning) << "There isn't an active user session, therefore it is not possible to execute commands under the users profile.";
      return nullptr;
    }

    // Get the user token for the active console session
    if (!WTSQueryUserToken(consoleSessionId, &userToken)) {
      BOOST_LOG(debug) << "QueryUserToken failed, this would prevent commands from launching under the users profile.";
      return nullptr;
    }

    // We need to know if this is an elevated token or not.
    // Get the elevation type of the user token
    GetTokenInformation(userToken, TokenElevationType, &elevationType, sizeof(TOKEN_ELEVATION_TYPE), &dwSize);

    // User has a limited token, this likely means they have UAC enabled.
    if (elevationType == TokenElevationTypeLimited) {
      TOKEN_LINKED_TOKEN linked_token;
      // Retrieve the administrator token that is linked to the limited token
      if (!GetTokenInformation(userToken, TokenLinkedToken, reinterpret_cast<void *>(&linked_token), sizeof(TOKEN_LINKED_TOKEN), &dwSize)) {
        DWORD lasterror = GetLastError();
        // If the retrieval failed, log an error message and return null
        BOOST_LOG(error) << "Request to elevate the users token had failed. Error: " << lasterror;
        return nullptr;
      }

      // Since we need the elevated token, we'll replace it with their administrative token.
      userToken = linked_token.LinkedToken;
    }


    // Use DuplicateTokenEx to create a primary token with maximum allowed access rights
    if (!DuplicateTokenEx(
          userToken,
          MAXIMUM_ALLOWED,
          NULL,
          SecurityIdentification,
          TokenPrimary,
          &duplicateToken)) {
      BOOST_LOG(debug) << "Error duplicating token";
      return nullptr;
    }

    return duplicateToken;
  }

  HANDLE
  duplicate_shell_token() {
    // Get the shell window (will usually be owned by explorer.exe)
    HWND shell_window = GetShellWindow();
    if (!shell_window) {
      BOOST_LOG(error) << "No shell window found. Is explorer.exe running?"sv;
      return NULL;
    }

    // Open a handle to the explorer.exe process
    DWORD shell_pid;
    GetWindowThreadProcessId(shell_window, &shell_pid);
    HANDLE shell_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
    if (!shell_process) {
      BOOST_LOG(error) << "Failed to open shell process: "sv << GetLastError();
      return NULL;
    }

    // Open explorer's token to clone for process creation
    HANDLE shell_token;
    BOOL ret = OpenProcessToken(shell_process, TOKEN_DUPLICATE, &shell_token);
    CloseHandle(shell_process);
    if (!ret) {
      BOOST_LOG(error) << "Failed to open shell process token: "sv << GetLastError();
      return NULL;
    }

    // Duplicate the token to make it usable for process creation
    HANDLE new_token;
    ret = DuplicateTokenEx(shell_token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &new_token);
    CloseHandle(shell_token);
    if (!ret) {
      BOOST_LOG(error) << "Failed to duplicate shell process token: "sv << GetLastError();
      return NULL;
    }

    return new_token;
  }

  PTOKEN_USER
  get_token_user(HANDLE token) {
    DWORD return_length;
    if (GetTokenInformation(token, TokenUser, NULL, 0, &return_length) || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to get token information size: "sv << winerr;
      return nullptr;
    }

    auto user = (PTOKEN_USER) HeapAlloc(GetProcessHeap(), 0, return_length);
    if (!user) {
      return nullptr;
    }

    if (!GetTokenInformation(token, TokenUser, user, return_length, &return_length)) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to get token information: "sv << winerr;
      HeapFree(GetProcessHeap(), 0, user);
      return nullptr;
    }

    return user;
  }

  void
  free_token_user(PTOKEN_USER user) {
    HeapFree(GetProcessHeap(), 0, user);
  }

  bool
  is_token_same_user_as_process(HANDLE other_token) {
    HANDLE process_token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token)) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to open process token: "sv << winerr;
      return false;
    }

    auto process_user = get_token_user(process_token);
    CloseHandle(process_token);
    if (!process_user) {
      return false;
    }

    auto token_user = get_token_user(other_token);
    if (!token_user) {
      free_token_user(process_user);
      return false;
    }

    bool ret = EqualSid(process_user->User.Sid, token_user->User.Sid);

    free_token_user(process_user);
    free_token_user(token_user);

    return ret;
  }

  bool
  merge_user_environment_block(bp::environment &env, HANDLE shell_token) {
    // Get the target user's environment block
    PVOID env_block;
    if (!CreateEnvironmentBlock(&env_block, shell_token, FALSE)) {
      return false;
    }

    // Parse the environment block and populate env
    for (auto c = (PWCHAR) env_block; *c != UNICODE_NULL; c += wcslen(c) + 1) {
      // Environment variable entries end with a null-terminator, so std::wstring() will get an entire entry.
      std::string env_tuple = wide_to_utf8_string(std::wstring { c });
      std::string env_name = env_tuple.substr(0, env_tuple.find('='));
      std::string env_val = env_tuple.substr(env_tuple.find('=') + 1);

      // Perform a case-insensitive search to see if this variable name already exists
      auto itr = std::find_if(env.cbegin(), env.cend(),
        [&](const auto &e) { return boost::iequals(e.get_name(), env_name); });
      if (itr != env.cend()) {
        // Use this existing name if it is already present to ensure we merge properly
        env_name = itr->get_name();
      }

      // For the PATH variable, we will merge the values together
      if (boost::iequals(env_name, "PATH")) {
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
  void
  append_string_to_environment_block(wchar_t *env_block, int &offset, const std::wstring &wstr) {
    std::memcpy(&env_block[offset], wstr.data(), wstr.length() * sizeof(wchar_t));
    offset += wstr.length();
  }

  std::wstring
  create_environment_block(bp::environment &env) {
    int size = 0;
    for (const auto &entry : env) {
      auto name = entry.get_name();
      auto value = entry.to_string();
      size += utf8_to_wide_string(name).length() + 1 /* L'=' */ + utf8_to_wide_string(value).length() + 1 /* L'\0' */;
    }

    size += 1 /* L'\0' */;

    wchar_t env_block[size];
    int offset = 0;
    for (const auto &entry : env) {
      auto name = entry.get_name();
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

  LPPROC_THREAD_ATTRIBUTE_LIST
  allocate_proc_thread_attr_list(DWORD attribute_count) {
    SIZE_T size;
    InitializeProcThreadAttributeList(NULL, attribute_count, 0, &size);

    auto list = (LPPROC_THREAD_ATTRIBUTE_LIST) HeapAlloc(GetProcessHeap(), 0, size);
    if (list == NULL) {
      return NULL;
    }

    if (!InitializeProcThreadAttributeList(list, attribute_count, 0, &size)) {
      HeapFree(GetProcessHeap(), 0, list);
      return NULL;
    }

    return list;
  }

  void
  free_proc_thread_attr_list(LPPROC_THREAD_ATTRIBUTE_LIST list) {
    DeleteProcThreadAttributeList(list);
    HeapFree(GetProcessHeap(), 0, list);
  }

  /**
 * @brief Creates a bp::child object from the results of launching a process
 *
 * @param process_launched A boolean indicating whether the launch was successful or not
 * @param cmd The command that was used to launch the process
 * @param ec A reference to an std::error_code object that will store any error that occurred during the launch
 * @param process_info A reference to a PROCESS_INFORMATION structure that contains information about the new process
 * @param group A pointer to a bp::group object that will add the new process to its group, if not null
 * @return A bp::child object representing the new process, or an empty bp::child object if the launch failed or an error occurred
 */
  bp::child
  create_boost_child_from_results(bool process_launched, const std::string &cmd, std::error_code &ec, PROCESS_INFORMATION &process_info, bp::group *group) {
    // Use RAII to ensure the process is closed when we're done with it, even if there was an error.
    auto close_process_handles = util::fail_guard([process_launched, process_info]() {
      if (process_launched) {
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
      }
    });

    if (ec) {
      // If there was an error, return an empty bp::child object
      return bp::child();
    }

    if (process_launched) {
      // If the launch was successful, create a new bp::child object representing the new process
      auto child = bp::child((bp::pid_t) process_info.dwProcessId);
      if (group) {
        // If a group was provided, add the new process to the group
        group->add(child);
      }

      BOOST_LOG(info) << cmd << " running with PID "sv << child.id();
      return child;
    }
    else {
      auto winerror = GetLastError();
      BOOST_LOG(error) << "Failed to launch process: "sv << winerror;
      ec = std::make_error_code(std::errc::invalid_argument);
      // We must NOT attach the failed process here, since this case can potentially be induced by ACL
      // manipulation (denying yourself execute permission) to cause an escalation of privilege.
      // So to protect ourselves against that, we'll return an empty child process instead.
      return bp::child();
    }
  }

  /**
 * @brief Impersonate the current user, invoke the callback function, then returns back to system context.
 *
 * @param user_token A handle to the user's token that was obtained from the shell
 * @param callback A function that will be executed while impersonating the user
 * @return An std::error_code object that will store any error that occurred during the impersonation
 */
  std::error_code
  impersonate_current_user(HANDLE user_token, std::function<void()> callback) {
    std::error_code ec;
    // Impersonate the user when launching the process. This will ensure that appropriate access
    // checks are done against the user token, not our SYSTEM token. It will also allow network
    // shares and mapped network drives to be used as launch targets, since those credentials
    // are stored per-user.
    if (!ImpersonateLoggedOnUser(user_token)) {
      auto winerror = GetLastError();
      // Log the failure of impersonating the user and its error code
      BOOST_LOG(error) << "Failed to impersonate user: "sv << winerror;
      ec = std::make_error_code(std::errc::permission_denied);
    }

    // Execute the callback function while impersonating the user
    callback();

    // End impersonation of the logged on user. If this fails (which is extremely unlikely),
    // we will be running with an unknown user token. The only safe thing to do in that case
    // is terminate ourselves.
    if (!RevertToSelf()) {
      auto winerror = GetLastError();
      // Log the failure of reverting to self and its error code
      BOOST_LOG(fatal) << "Failed to revert to self after impersonation: "sv << winerror;
      std::abort();
    }

    return ec;
  }

  /**
 * @brief A function to create a STARTUPINFOEXW structure for launching a process
 *
 * @param file A pointer to a FILE object that will be used as the standard output and error for the new process, or null if not needed
 * @param ec A reference to an std::error_code object that will store any error that occurred during the creation of the structure
 * @return A STARTUPINFOEXW structure that contains information about how to launch the new process
 */
  STARTUPINFOEXW
  create_startup_info(FILE *file, std::error_code &ec) {
    // Initialize a zeroed-out STARTUPINFOEXW structure and set its size
    STARTUPINFOEXW startup_info = {};
    startup_info.StartupInfo.cb = sizeof(startup_info);

    // Allocate a process attribute list with space for 1 element
    startup_info.lpAttributeList = allocate_proc_thread_attr_list(1);
    if (startup_info.lpAttributeList == NULL) {
      // If the allocation failed, set ec to an appropriate error code and return the structure
      ec = std::make_error_code(std::errc::not_enough_memory);
      return startup_info;
    }

    if (file) {
      // If a file was provided, get its handle and use it as the standard output and error for the new process
      HANDLE log_file_handle = (HANDLE) _get_osfhandle(_fileno(file));

      // Populate std handles if the caller gave us a log file to use
      startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
      startup_info.StartupInfo.hStdInput = NULL;
      startup_info.StartupInfo.hStdOutput = log_file_handle;
      startup_info.StartupInfo.hStdError = log_file_handle;

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

    return startup_info;
  }

  /**
 * @brief Runs a command on the users profile, with elevation
 *
 * This function launches a child process with elevated privileges, using the current user's environment
 * and a specific working directory. If the launch is successful, a `bp::child` object representing the new
 * process is returned. Otherwise, an error code is returned.
 *
 * @param cmd The command to run
 * @param working_dir The working directory for the new process
 * @param env The environment variables to use for the new process
 * @param file A file object to redirect the child process's output to (may be nullptr)
 * @param ec An error code, set to indicate any errors that occur during the launch process
 * @param group A pointer to a `bp::group` object to which the new process should belong (may be nullptr)
 *
 * @return A `bp::child` object representing the new process, or an empty `bp::child` object if the launch fails
 */
  bp::child
  run_priviliged(const std::string &cmd, boost::filesystem::path &working_dir, boost::process::environment &env, FILE *file, std::error_code &ec, boost::process::group *group) {
    PROCESS_INFORMATION process_info;
    BOOL ret;

    // Duplicate the current user's token with elevated privileges
    HANDLE users_token = duplicate_users_token_elevated();
    if (!users_token) {
      // This can happen if the shell has crashed. Fail the launch rather than risking launching with
      // Sunshine's permissions unmodified.
      ec = std::make_error_code(std::errc::no_such_process);
      BOOST_LOG(warning) << "Unable to clone token:  " << GetLastError();
      return bp::child();
    }

    // Use RAII to ensure the token is closed when we're done with it
    auto token_close = util::fail_guard([users_token]() {
      CloseHandle(users_token);
    });

    // Populate env with user-specific environment variables
    if (!merge_user_environment_block(env, users_token)) {
      BOOST_LOG(warning) << "Unable to merge environment block " << GetLastError();
      ec = std::make_error_code(std::errc::not_enough_memory);
      return bp::child();
    }

    // Most Win32 APIs can't consume UTF-8 strings directly, so we must convert them into UTF-16
    std::wstring wcmd = utf8_to_wide_string(cmd);
    std::wstring env_block = create_environment_block(env);
    std::wstring start_dir = utf8_to_wide_string(working_dir.string());

    // Create the STARTUPINFOEXW object for the new process
    STARTUPINFOEXW startup_info = create_startup_info(file, ec);

    // Use RAII to ensure the attribute list is freed when we're done with it
    auto attr_list_free = util::fail_guard([list = startup_info.lpAttributeList]() {
      free_proc_thread_attr_list(list);
    });

    // Impersonate the current user and launch the new process
    ec = impersonate_current_user(users_token, [&]() {
      ret = CreateProcessAsUserW(users_token,
        NULL,
        (LPWSTR) wcmd.c_str(),
        NULL,
        NULL,
        !!(startup_info.StartupInfo.dwFlags & STARTF_USESTDHANDLES),
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB,
        env_block.data(),
        start_dir.empty() ? NULL : start_dir.c_str(),
        (LPSTARTUPINFOW) &startup_info,
        &process_info);
    });  // Use the results of the launch to create a bp::child object
    return create_boost_child_from_results(ret, cmd, ec, process_info, group);
  }

  /**
 * @brief Runs a command on the users profile, without elevation
 *
 * This function launches a child process as an unprivileged user, using the current user's environment
 * and a specific working directory. If the launch is successful, a `bp::child` object representing the new
 * process is returned. Otherwise, an error code is returned.
 *
 * @param cmd The command to run
 * @param working_dir The working directory for the new process
 * @param env The environment variables to use for the new process
 * @param file A file object to redirect the child process's output to (may be nullptr)
 * @param ec An error code, set to indicate any errors that occur during the launch process
 * @param group A pointer to a `bp::group` object to which the new process should belong (may be nullptr)
 *
 * @return A `bp::child` object representing the new process, or an empty `bp::child` object if the launch fails
 */
  bp::child
  run_unprivileged(const std::string &cmd, boost::filesystem::path &working_dir, bp::environment &env, FILE *file, std::error_code &ec, bp::group *group) {
    // Duplicate the current user's shell token
    HANDLE shell_token = duplicate_shell_token();
    if (!shell_token) {
      // This can happen if the shell has crashed. Fail the launch rather than risking launching with
      // Sunshine's permissions unmodified.
      ec = std::make_error_code(std::errc::no_such_process);
      return bp::child();
    }

    // Use RAII to ensure the shell token is closed when we're done with it
    auto token_close = util::fail_guard([shell_token]() {
      CloseHandle(shell_token);
    });

    // Populate env with user-specific environment variables
    if (!merge_user_environment_block(env, shell_token)) {
      ec = std::make_error_code(std::errc::not_enough_memory);
      return bp::child();
    }

    // Convert cmd, env, and working_dir to the appropriate character sets for Win32 APIs
    std::wstring wcmd = utf8_to_wide_string(cmd);
    std::wstring env_block = create_environment_block(env);
    std::wstring start_dir = utf8_to_wide_string(working_dir.string());

    // Create the STARTUPINFOEXW object for the new process
    STARTUPINFOEXW startup_info = create_startup_info(file, ec);

    // Use RAII to ensure the attribute list is freed when we're done with it
    auto attr_list_free = util::fail_guard([list = startup_info.lpAttributeList]() {
      free_proc_thread_attr_list(list);
    });

    PROCESS_INFORMATION process_info;
    BOOL ret;
    // If the shell token is for a different user account, launch the process using CreateProcessAsUserW()
    if (!is_token_same_user_as_process(shell_token)) {
      ec = impersonate_current_user(shell_token, [&]() {
        ret = CreateProcessAsUserW(shell_token,
          NULL,
          (LPWSTR) wcmd.c_str(),
          NULL,
          NULL,
          !!(startup_info.StartupInfo.dwFlags & STARTF_USESTDHANDLES),
          EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB,
          env_block.data(),
          start_dir.empty() ? NULL : start_dir.c_str(),
          (LPSTARTUPINFOW) &startup_info,
          &process_info);
      });

      if (ec) {
        return bp::child();
      }
    }
    // Otherwise, launch the process using CreateProcessW()
    else {
      ret = CreateProcessW(NULL,
        (LPWSTR) wcmd.c_str(),
        NULL,
        NULL,
        !!(startup_info.StartupInfo.dwFlags & STARTF_USESTDHANDLES),
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB,
        env_block.data(),
        start_dir.empty() ? NULL : start_dir.c_str(),
        (LPSTARTUPINFOW) &startup_info,
        &process_info);
    }

    // Use the results of the launch to create a bp::child object
    return create_boost_child_from_results(ret, cmd, ec, process_info, group);
  }

  void
  adjust_thread_priority(thread_priority_e priority) {
    int win32_priority;

    switch (priority) {
      case thread_priority_e::low:
        win32_priority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
      case thread_priority_e::normal:
        win32_priority = THREAD_PRIORITY_NORMAL;
        break;
      case thread_priority_e::high:
        win32_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
      case thread_priority_e::critical:
        win32_priority = THREAD_PRIORITY_HIGHEST;
        break;
      default:
        BOOST_LOG(error) << "Unknown thread priority: "sv << (int) priority;
        return;
    }

    if (!SetThreadPriority(GetCurrentThread(), win32_priority)) {
      auto winerr = GetLastError();
      BOOST_LOG(warning) << "Unable to set thread priority to "sv << win32_priority << ": "sv << winerr;
    }
  }

  void
  streaming_will_start() {
    static std::once_flag load_wlanapi_once_flag;
    std::call_once(load_wlanapi_once_flag, []() {
      // wlanapi.dll is not installed by default on Windows Server, so we load it dynamically
      HMODULE wlanapi = LoadLibraryExA("wlanapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (!wlanapi) {
        BOOST_LOG(debug) << "wlanapi.dll is not available on this OS"sv;
        return;
      }

      fn_WlanOpenHandle = (decltype(fn_WlanOpenHandle)) GetProcAddress(wlanapi, "WlanOpenHandle");
      fn_WlanCloseHandle = (decltype(fn_WlanCloseHandle)) GetProcAddress(wlanapi, "WlanCloseHandle");
      fn_WlanFreeMemory = (decltype(fn_WlanFreeMemory)) GetProcAddress(wlanapi, "WlanFreeMemory");
      fn_WlanEnumInterfaces = (decltype(fn_WlanEnumInterfaces)) GetProcAddress(wlanapi, "WlanEnumInterfaces");
      fn_WlanSetInterface = (decltype(fn_WlanSetInterface)) GetProcAddress(wlanapi, "WlanSetInterface");

      if (!fn_WlanOpenHandle || !fn_WlanCloseHandle || !fn_WlanFreeMemory || !fn_WlanEnumInterfaces || !fn_WlanSetInterface) {
        BOOST_LOG(error) << "wlanapi.dll is missing exports?"sv;

        fn_WlanOpenHandle = nullptr;
        fn_WlanCloseHandle = nullptr;
        fn_WlanFreeMemory = nullptr;
        fn_WlanEnumInterfaces = nullptr;
        fn_WlanSetInterface = nullptr;

        FreeLibrary(wlanapi);
        return;
      }
    });

    // Enable MMCSS scheduling for DWM
    DwmEnableMMCSS(true);

    // Reduce timer period to 1ms
    timeBeginPeriod(1);

    // Promote ourselves to high priority class
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    // Enable low latency mode on all connected WLAN NICs if wlanapi.dll is available
    if (fn_WlanOpenHandle) {
      DWORD negotiated_version;

      if (fn_WlanOpenHandle(WLAN_API_MAKE_VERSION(2, 0), nullptr, &negotiated_version, &wlan_handle) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST wlan_interface_list;

        if (fn_WlanEnumInterfaces(wlan_handle, nullptr, &wlan_interface_list) == ERROR_SUCCESS) {
          for (DWORD i = 0; i < wlan_interface_list->dwNumberOfItems; i++) {
            if (wlan_interface_list->InterfaceInfo[i].isState == wlan_interface_state_connected) {
              // Enable media streaming mode for 802.11 wireless interfaces to reduce latency and
              // unneccessary background scanning operations that cause packet loss and jitter.
              //
              // https://docs.microsoft.com/en-us/windows-hardware/drivers/network/oid-wdi-set-connection-quality
              // https://docs.microsoft.com/en-us/previous-versions/windows/hardware/wireless/native-802-11-media-streaming
              BOOL value = TRUE;
              auto error = fn_WlanSetInterface(wlan_handle, &wlan_interface_list->InterfaceInfo[i].InterfaceGuid,
                wlan_intf_opcode_media_streaming_mode, sizeof(value), &value, nullptr);
              if (error == ERROR_SUCCESS) {
                BOOST_LOG(info) << "WLAN interface "sv << i << " is now in low latency mode"sv;
              }
            }
          }

          fn_WlanFreeMemory(wlan_interface_list);
        }
        else {
          fn_WlanCloseHandle(wlan_handle, nullptr);
          wlan_handle = NULL;
        }
      }
    }

    // If there is no mouse connected, enable Mouse Keys to force the cursor to appear
    if (!GetSystemMetrics(SM_MOUSEPRESENT)) {
      BOOST_LOG(info) << "A mouse was not detected. Sunshine will enable Mouse Keys while streaming to force the mouse cursor to appear.";

      // Get the current state of Mouse Keys so we can restore it when streaming is over
      previous_mouse_keys_state.cbSize = sizeof(previous_mouse_keys_state);
      if (SystemParametersInfoW(SPI_GETMOUSEKEYS, 0, &previous_mouse_keys_state, 0)) {
        MOUSEKEYS new_mouse_keys_state = {};

        // Enable Mouse Keys
        new_mouse_keys_state.cbSize = sizeof(new_mouse_keys_state);
        new_mouse_keys_state.dwFlags = MKF_MOUSEKEYSON | MKF_AVAILABLE;
        new_mouse_keys_state.iMaxSpeed = 10;
        new_mouse_keys_state.iTimeToMaxSpeed = 1000;
        if (SystemParametersInfoW(SPI_SETMOUSEKEYS, 0, &new_mouse_keys_state, 0)) {
          // Remember to restore the previous settings when we stop streaming
          enabled_mouse_keys = true;
        }
        else {
          auto winerr = GetLastError();
          BOOST_LOG(warning) << "Unable to enable Mouse Keys: "sv << winerr;
        }
      }
      else {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "Unable to get current state of Mouse Keys: "sv << winerr;
      }
    }
  }

  void
  streaming_will_stop() {
    // Demote ourselves back to normal priority class
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    // End our 1ms timer request
    timeEndPeriod(1);

    // Disable MMCSS scheduling for DWM
    DwmEnableMMCSS(false);

    // Closing our WLAN client handle will undo our optimizations
    if (wlan_handle != nullptr) {
      fn_WlanCloseHandle(wlan_handle, nullptr);
      wlan_handle = nullptr;
    }

    // Restore Mouse Keys back to the previous settings if we turned it on
    if (enabled_mouse_keys) {
      enabled_mouse_keys = false;
      if (!SystemParametersInfoW(SPI_SETMOUSEKEYS, 0, &previous_mouse_keys_state, 0)) {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "Unable to restore original state of Mouse Keys: "sv << winerr;
      }
    }
  }

  bool
  restart_supported() {
    // Restart is supported if we're running from the service
    return (GetConsoleWindow() == NULL);
  }

  bool
  restart() {
    // Gracefully exit. The service will restart us in a few seconds.
    // We use an async exit call here because we can't block the
    // HTTP thread or we'll hang shutdown.
    lifetime::exit_sunshine(0, true);
    return true;
  }

  SOCKADDR_IN
  to_sockaddr(boost::asio::ip::address_v4 address, uint16_t port) {
    SOCKADDR_IN saddr_v4 = {};

    saddr_v4.sin_family = AF_INET;
    saddr_v4.sin_port = htons(port);

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v4.sin_addr, addr_bytes.data(), sizeof(saddr_v4.sin_addr));

    return saddr_v4;
  }

  SOCKADDR_IN6
  to_sockaddr(boost::asio::ip::address_v6 address, uint16_t port) {
    SOCKADDR_IN6 saddr_v6 = {};

    saddr_v6.sin6_family = AF_INET6;
    saddr_v6.sin6_port = htons(port);
    saddr_v6.sin6_scope_id = address.scope_id();

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v6.sin6_addr, addr_bytes.data(), sizeof(saddr_v6.sin6_addr));

    return saddr_v6;
  }

  // Use UDP segmentation offload if it is supported by the OS. If the NIC is capable, this will use
  // hardware acceleration to reduce CPU usage. Support for USO was introduced in Windows 10 20H1.
  bool
  send_batch(batched_send_info_t &send_info) {
    WSAMSG msg;

    // Convert the target address into a SOCKADDR
    SOCKADDR_IN saddr_v4;
    SOCKADDR_IN6 saddr_v6;
    if (send_info.target_address.is_v6()) {
      saddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.name = (PSOCKADDR) &saddr_v6;
      msg.namelen = sizeof(saddr_v6);
    }
    else {
      saddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.name = (PSOCKADDR) &saddr_v4;
      msg.namelen = sizeof(saddr_v4);
    }

    WSABUF buf;
    buf.buf = (char *) send_info.buffer;
    buf.len = send_info.block_size * send_info.block_count;

    msg.lpBuffers = &buf;
    msg.dwBufferCount = 1;
    msg.dwFlags = 0;

    char cmbuf[WSA_CMSG_SPACE(sizeof(DWORD))];
    msg.Control.buf = cmbuf;
    msg.Control.len = 0;

    if (send_info.block_count > 1) {
      msg.Control.len += WSA_CMSG_SPACE(sizeof(DWORD));

      auto cm = WSA_CMSG_FIRSTHDR(&msg);
      cm->cmsg_level = IPPROTO_UDP;
      cm->cmsg_type = UDP_SEND_MSG_SIZE;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(DWORD));
      *((DWORD *) WSA_CMSG_DATA(cm)) = send_info.block_size;
    }

    // If USO is not supported, this will fail and the caller will fall back to unbatched sends.
    DWORD bytes_sent;
    return WSASendMsg((SOCKET) send_info.native_socket, &msg, 1, &bytes_sent, nullptr, nullptr) != SOCKET_ERROR;
  }

  class qos_t: public deinit_t {
  public:
    qos_t(QOS_FLOWID flow_id):
        flow_id(flow_id) {}

    virtual ~qos_t() {
      if (!fn_QOSRemoveSocketFromFlow(qos_handle, (SOCKET) NULL, flow_id, 0)) {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "QOSRemoveSocketFromFlow() failed: "sv << winerr;
      }
    }

  private:
    QOS_FLOWID flow_id;
  };

  std::unique_ptr<deinit_t>
  enable_socket_qos(uintptr_t native_socket, boost::asio::ip::address &address, uint16_t port, qos_data_type_e data_type) {
    SOCKADDR_IN saddr_v4;
    SOCKADDR_IN6 saddr_v6;
    PSOCKADDR dest_addr;

    static std::once_flag load_qwave_once_flag;
    std::call_once(load_qwave_once_flag, []() {
      // qWAVE is not installed by default on Windows Server, so we load it dynamically
      HMODULE qwave = LoadLibraryExA("qwave.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (!qwave) {
        BOOST_LOG(debug) << "qwave.dll is not available on this OS"sv;
        return;
      }

      fn_QOSCreateHandle = (decltype(fn_QOSCreateHandle)) GetProcAddress(qwave, "QOSCreateHandle");
      fn_QOSAddSocketToFlow = (decltype(fn_QOSAddSocketToFlow)) GetProcAddress(qwave, "QOSAddSocketToFlow");
      fn_QOSRemoveSocketFromFlow = (decltype(fn_QOSRemoveSocketFromFlow)) GetProcAddress(qwave, "QOSRemoveSocketFromFlow");

      if (!fn_QOSCreateHandle || !fn_QOSAddSocketToFlow || !fn_QOSRemoveSocketFromFlow) {
        BOOST_LOG(error) << "qwave.dll is missing exports?"sv;

        fn_QOSCreateHandle = nullptr;
        fn_QOSAddSocketToFlow = nullptr;
        fn_QOSRemoveSocketFromFlow = nullptr;

        FreeLibrary(qwave);
        return;
      }

      QOS_VERSION qos_version { 1, 0 };
      if (!fn_QOSCreateHandle(&qos_version, &qos_handle)) {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "QOSCreateHandle() failed: "sv << winerr;
        return;
      }
    });

    // If qWAVE is unavailable, just return
    if (!fn_QOSAddSocketToFlow || !qos_handle) {
      return nullptr;
    }

    if (address.is_v6()) {
      saddr_v6 = to_sockaddr(address.to_v6(), port);
      dest_addr = (PSOCKADDR) &saddr_v6;
    }
    else {
      saddr_v4 = to_sockaddr(address.to_v4(), port);
      dest_addr = (PSOCKADDR) &saddr_v4;
    }

    QOS_TRAFFIC_TYPE traffic_type;
    switch (data_type) {
      case qos_data_type_e::audio:
        traffic_type = QOSTrafficTypeVoice;
        break;
      case qos_data_type_e::video:
        traffic_type = QOSTrafficTypeAudioVideo;
        break;
      default:
        BOOST_LOG(error) << "Unknown traffic type: "sv << (int) data_type;
        return nullptr;
    }

    QOS_FLOWID flow_id = 0;
    if (!fn_QOSAddSocketToFlow(qos_handle, (SOCKET) native_socket, dest_addr, traffic_type, QOS_NON_ADAPTIVE_FLOW, &flow_id)) {
      auto winerr = GetLastError();
      BOOST_LOG(warning) << "QOSAddSocketToFlow() failed: "sv << winerr;
      return nullptr;
    }

    return std::make_unique<qos_t>(flow_id);
  }
}  // namespace platf