/**
 * @file src/platform/windows/misc.cpp
 * @brief todo
 */
#include <codecvt>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <set>
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

#include "nvprefs/nvprefs_interface.h"

// UDP_SEND_MSG_SIZE was added in the Windows 10 20H1 SDK
#ifndef UDP_SEND_MSG_SIZE
  #define UDP_SEND_MSG_SIZE 2
#endif

// PROC_THREAD_ATTRIBUTE_JOB_LIST is currently missing from MinGW headers
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
  #define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

#include <qos2.h>

#ifndef WLAN_API_MAKE_VERSION
  #define WLAN_API_MAKE_VERSION(_major, _minor) (((DWORD) (_minor)) << 16 | (_major))
#endif

namespace bp = boost::process;

using namespace std::literals;
namespace platf {
  using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

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
    char data[INET6_ADDRSTRLEN] = {};

    auto family = socket_address->sa_family;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) socket_address)->sin6_addr, data, INET6_ADDRSTRLEN);
    }
    else if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) socket_address)->sin_addr, data, INET_ADDRSTRLEN);
    }

    return std::string { data };
  }

  std::pair<std::uint16_t, std::string>
  from_sockaddr_ex(const sockaddr *const ip_addr) {
    char data[INET6_ADDRSTRLEN] = {};

    auto family = ip_addr->sa_family;
    std::uint16_t port = 0;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
      port = ((sockaddr_in6 *) ip_addr)->sin6_port;
    }
    else if (family == AF_INET) {
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

  bool
  IsUserAdmin(HANDLE user_token) {
    WINBOOL ret;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;
    ret = AllocateAndInitializeSid(
      &NtAuthority,
      2,
      SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_ADMINS,
      0, 0, 0, 0, 0, 0,
      &AdministratorsGroup);
    if (ret) {
      if (!CheckTokenMembership(user_token, AdministratorsGroup, &ret)) {
        ret = false;
        BOOST_LOG(error) << "Failed to verify token membership for administrative access: " << GetLastError();
      }
      FreeSid(AdministratorsGroup);
    }
    else {
      BOOST_LOG(error) << "Unable to allocate SID to check administrative access: " << GetLastError();
    }

    return ret;
  }

  /**
   * @brief Obtain the current sessions user's primary token with elevated privileges.
   * @return The user's token. If user has admin capability it will be elevated, otherwise it will be a limited token. On error, `nullptr`.
   */
  HANDLE
  retrieve_users_token(bool elevated) {
    DWORD consoleSessionId;
    HANDLE userToken;
    TOKEN_ELEVATION_TYPE elevationType;
    DWORD dwSize;

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
    // Elevation - Default: User is not an admin, UAC enabled/disabled does not matter.
    // Elevation - Limited: User is an admin, has UAC enabled.
    // Elevation - Full:    User is an admin, has UAC disabled.
    if (!GetTokenInformation(userToken, TokenElevationType, &elevationType, sizeof(TOKEN_ELEVATION_TYPE), &dwSize)) {
      BOOST_LOG(debug) << "Retrieving token information failed: " << GetLastError();
      CloseHandle(userToken);
      return nullptr;
    }

    // User is currently not an administrator
    // The documentation for this scenario is conflicting, so we'll double check to see if user is actually an admin.
    if (elevated && (elevationType == TokenElevationTypeDefault && !IsUserAdmin(userToken))) {
      // We don't have to strip the token or do anything here, but let's give the user a warning so they're aware what is happening.
      BOOST_LOG(warning) << "This command requires elevation and the current user account logged in does not have administrator rights. "
                         << "For security reasons Sunshine will retain the same access level as the current user and will not elevate it.";
    }

    // User has a limited token, this means they have UAC enabled and is an Administrator
    if (elevated && elevationType == TokenElevationTypeLimited) {
      TOKEN_LINKED_TOKEN linkedToken;
      // Retrieve the administrator token that is linked to the limited token
      if (!GetTokenInformation(userToken, TokenLinkedToken, reinterpret_cast<void *>(&linkedToken), sizeof(TOKEN_LINKED_TOKEN), &dwSize)) {
        // If the retrieval failed, log an error message and return null
        BOOST_LOG(error) << "Retrieving linked token information failed: " << GetLastError();
        CloseHandle(userToken);

        // There is no scenario where this should be hit, except for an actual error.
        return nullptr;
      }

      // Since we need the elevated token, we'll replace it with their administrative token.
      CloseHandle(userToken);
      userToken = linkedToken.LinkedToken;
    }

    // We don't need to do anything for TokenElevationTypeFull users here, because they're already elevated.
    return userToken;
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
      std::string env_tuple = converter.to_bytes(std::wstring { c });
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

  /**
   * @brief Check if the current process is running with system-level privileges.
   * @return `true` if the current process has system-level privileges, `false` otherwise.
   */
  bool
  is_running_as_system() {
    BOOL ret;
    PSID SystemSid;
    DWORD dwSize = SECURITY_MAX_SID_SIZE;

    // Allocate memory for the SID structure
    SystemSid = LocalAlloc(LMEM_FIXED, dwSize);
    if (SystemSid == nullptr) {
      BOOST_LOG(error) << "Failed to allocate memory for the SID structure: " << GetLastError();
      return false;
    }

    // Create a SID for the local system account
    ret = CreateWellKnownSid(WinLocalSystemSid, nullptr, SystemSid, &dwSize);
    if (ret) {
      // Check if the current process token contains this SID
      if (!CheckTokenMembership(nullptr, SystemSid, &ret)) {
        BOOST_LOG(error) << "Failed to check token membership: " << GetLastError();
        ret = false;
      }
    }
    else {
      BOOST_LOG(error) << "Failed to create a SID for the local system account. This may happen if the system is out of memory or if the SID buffer is too small: " << GetLastError();
    }

    // Free the memory allocated for the SID structure
    LocalFree(SystemSid);
    return ret;
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
      size += converter.from_bytes(name).length() + 1 /* L'=' */ + converter.from_bytes(value).length() + 1 /* L'\0' */;
    }

    size += 1 /* L'\0' */;

    wchar_t env_block[size];
    int offset = 0;
    for (const auto &entry : env) {
      auto name = entry.get_name();
      auto value = entry.to_string();

      // Construct the NAME=VAL\0 string
      append_string_to_environment_block(env_block, offset, converter.from_bytes(name));
      env_block[offset++] = L'=';
      append_string_to_environment_block(env_block, offset, converter.from_bytes(value));
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
   * @brief Create a `bp::child` object from the results of launching a process.
   * @param process_launched A boolean indicating if the launch was successful.
   * @param cmd The command that was used to launch the process.
   * @param ec A reference to an `std::error_code` object that will store any error that occurred during the launch.
   * @param process_info A reference to a `PROCESS_INFORMATION` structure that contains information about the new process.
   * @return A `bp::child` object representing the new process, or an empty `bp::child` object if the launch failed.
   */
  bp::child
  create_boost_child_from_results(bool process_launched, const std::string &cmd, std::error_code &ec, PROCESS_INFORMATION &process_info) {
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
   * @brief Impersonate the current user and invoke the callback function.
   * @param user_token A handle to the user's token that was obtained from the shell.
   * @param callback A function that will be executed while impersonating the user.
   * @return An `std::error_code` object that will store any error that occurred during the impersonation
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
      return ec;
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
   * @brief A function to create a `STARTUPINFOEXW` structure for launching a process.
   * @param file A pointer to a `FILE` object that will be used as the standard output and error for the new process, or null if not needed.
   * @param job A job object handle to insert the new process into. This pointer must remain valid for the life of this startup info!
   * @param ec A reference to a `std::error_code` object that will store any error that occurred during the creation of the structure.
   * @return A `STARTUPINFOEXW` structure that contains information about how to launch the new process.
   */
  STARTUPINFOEXW
  create_startup_info(FILE *file, HANDLE *job, std::error_code &ec) {
    // Initialize a zeroed-out STARTUPINFOEXW structure and set its size
    STARTUPINFOEXW startup_info = {};
    startup_info.StartupInfo.cb = sizeof(startup_info);

    // Allocate a process attribute list with space for 2 elements
    startup_info.lpAttributeList = allocate_proc_thread_attr_list(2);
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
      //
      // Note: The value we point to here must be valid for the lifetime of the attribute list,
      // so we need to point into the STARTUPINFO instead of our log_file_variable on the stack.
      UpdateProcThreadAttribute(startup_info.lpAttributeList,
        0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        &startup_info.StartupInfo.hStdOutput,
        sizeof(startup_info.StartupInfo.hStdOutput),
        NULL,
        NULL);
    }

    if (job) {
      // Atomically insert the new process into the specified job.
      //
      // Note: The value we point to here must be valid for the lifetime of the attribute list,
      // so we take a HANDLE* instead of just a HANDLE to use the caller's stack storage.
      UpdateProcThreadAttribute(startup_info.lpAttributeList,
        0,
        PROC_THREAD_ATTRIBUTE_JOB_LIST,
        job,
        sizeof(*job),
        NULL,
        NULL);
    }

    return startup_info;
  }

  /**
   * @brief Run a command on the users profile.
   *
   * Launches a child process as the user, using the current user's environment and a specific working directory.
   *
   * @param elevated Specify whether to elevate the process.
   * @param interactive Specify whether this will run in a window or hidden.
   * @param cmd The command to run.
   * @param working_dir The working directory for the new process.
   * @param env The environment variables to use for the new process.
   * @param file A file object to redirect the child process's output to (may be `nullptr`).
   * @param ec An error code, set to indicate any errors that occur during the launch process.
   * @param group A pointer to a `bp::group` object to which the new process should belong (may be `nullptr`).
   * @return A `bp::child` object representing the new process, or an empty `bp::child` object if the launch fails.
   */
  bp::child
  run_command(bool elevated, bool interactive, const std::string &cmd, boost::filesystem::path &working_dir, const bp::environment &env, FILE *file, std::error_code &ec, bp::group *group) {
    BOOL ret;
    // Convert cmd, env, and working_dir to the appropriate character sets for Win32 APIs
    std::wstring wcmd = converter.from_bytes(cmd);
    std::wstring start_dir = converter.from_bytes(working_dir.string());

    HANDLE job = group ? group->native_handle() : nullptr;
    STARTUPINFOEXW startup_info = create_startup_info(file, job ? &job : nullptr, ec);
    PROCESS_INFORMATION process_info;

    // Clone the environment to create a local copy. Boost.Process (bp) shares the environment with all spawned processes.
    // Since we're going to modify the 'env' variable by merging user-specific environment variables into it,
    // we make a clone to prevent side effects to the shared environment.
    bp::environment cloned_env = env;

    if (ec) {
      // In the event that startup_info failed, return a blank child process.
      return bp::child();
    }

    // Use RAII to ensure the attribute list is freed when we're done with it
    auto attr_list_free = util::fail_guard([list = startup_info.lpAttributeList]() {
      free_proc_thread_attr_list(list);
    });

    DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB;

    // Create a new console for interactive processes and use no console for non-interactive processes
    creation_flags |= interactive ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;

    if (is_running_as_system()) {
      // Duplicate the current user's token
      HANDLE user_token = retrieve_users_token(elevated);
      if (!user_token) {
        // Fail the launch rather than risking launching with Sunshine's permissions unmodified.
        ec = std::make_error_code(std::errc::permission_denied);
        return bp::child();
      }

      // Use RAII to ensure the shell token is closed when we're done with it
      auto token_close = util::fail_guard([user_token]() {
        CloseHandle(user_token);
      });

      // Populate env with user-specific environment variables
      if (!merge_user_environment_block(cloned_env, user_token)) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return bp::child();
      }

      // Open the process as the current user account, elevation is handled in the token itself.
      ec = impersonate_current_user(user_token, [&]() {
        std::wstring env_block = create_environment_block(cloned_env);
        ret = CreateProcessAsUserW(user_token,
          NULL,
          (LPWSTR) wcmd.c_str(),
          NULL,
          NULL,
          !!(startup_info.StartupInfo.dwFlags & STARTF_USESTDHANDLES),
          creation_flags,
          env_block.data(),
          start_dir.empty() ? NULL : start_dir.c_str(),
          (LPSTARTUPINFOW) &startup_info,
          &process_info);
      });
    }
    // Otherwise, launch the process using CreateProcessW()
    // This will inherit the elevation of whatever the user launched Sunshine with.
    else {
      // Open our current token to resolve environment variables
      HANDLE process_token;
      if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &process_token)) {
        ec = std::make_error_code(std::errc::permission_denied);
        return bp::child();
      }
      auto token_close = util::fail_guard([process_token]() {
        CloseHandle(process_token);
      });

      // Populate env with user-specific environment variables
      if (!merge_user_environment_block(cloned_env, process_token)) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return bp::child();
      }

      std::wstring env_block = create_environment_block(cloned_env);
      ret = CreateProcessW(NULL,
        (LPWSTR) wcmd.c_str(),
        NULL,
        NULL,
        !!(startup_info.StartupInfo.dwFlags & STARTF_USESTDHANDLES),
        creation_flags,
        env_block.data(),
        start_dir.empty() ? NULL : start_dir.c_str(),
        (LPSTARTUPINFOW) &startup_info,
        &process_info);
    }

    // Use the results of the launch to create a bp::child object
    return create_boost_child_from_results(ret, cmd, ec, process_info);
  }

  /**
   * @brief Open a url in the default web browser.
   * @param url The url to open.
   */
  void
  open_url(const std::string &url) {
    // set working dir to Windows system directory
    auto working_dir = boost::filesystem::path(std::getenv("SystemRoot"));

    boost::process::environment _env = boost::this_process::environment();
    std::error_code ec;

    // Launch this as a non-interactive non-elevated command to avoid an extra console window
    std::string cmd = R"(cmd /C "start )" + url + R"(")";
    auto child = run_command(false, false, cmd, working_dir, _env, nullptr, ec, nullptr);
    if (ec) {
      BOOST_LOG(warning) << "Couldn't open url ["sv << url << "]: System: "sv << ec.message();
    }
    else {
      BOOST_LOG(info) << "Opened url ["sv << url << "]"sv;
      child.detach();
    }
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

    // Modify NVIDIA control panel settings again, in case they have been changed externally since sunshine launch
    if (nvprefs_instance.load()) {
      if (!nvprefs_instance.owning_undo_file()) {
        nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
      }
      nvprefs_instance.modify_application_profile();
      nvprefs_instance.modify_global_profile();
      nvprefs_instance.unload();
    }

    // Enable low latency mode on all connected WLAN NICs if wlanapi.dll is available
    if (fn_WlanOpenHandle) {
      DWORD negotiated_version;

      if (fn_WlanOpenHandle(WLAN_API_MAKE_VERSION(2, 0), nullptr, &negotiated_version, &wlan_handle) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST wlan_interface_list;

        if (fn_WlanEnumInterfaces(wlan_handle, nullptr, &wlan_interface_list) == ERROR_SUCCESS) {
          for (DWORD i = 0; i < wlan_interface_list->dwNumberOfItems; i++) {
            if (wlan_interface_list->InterfaceInfo[i].isState == wlan_interface_state_connected) {
              // Enable media streaming mode for 802.11 wireless interfaces to reduce latency and
              // unnecessary background scanning operations that cause packet loss and jitter.
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

  void
  restart_on_exit() {
    STARTUPINFOEXW startup_info {};
    startup_info.StartupInfo.cb = sizeof(startup_info);

    WCHAR executable[MAX_PATH];
    if (GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable)) == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(fatal) << "Failed to get Sunshine path: "sv << winerr;
      return;
    }

    PROCESS_INFORMATION process_info;
    if (!CreateProcessW(executable,
          GetCommandLineW(),
          nullptr,
          nullptr,
          false,
          CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
          nullptr,
          nullptr,
          (LPSTARTUPINFOW) &startup_info,
          &process_info)) {
      auto winerr = GetLastError();
      BOOST_LOG(fatal) << "Unable to restart Sunshine: "sv << winerr;
      return;
    }

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
  }

  void
  restart() {
    // If we're running standalone, we have to respawn ourselves via CreateProcess().
    // If we're running from the service, we should just exit and let it respawn us.
    if (GetConsoleWindow() != NULL) {
      // Avoid racing with the new process by waiting until we're exiting to start it.
      atexit(restart_on_exit);
    }

    // We use an async exit call here because we can't block the HTTP thread or we'll hang shutdown.
    lifetime::exit_sunshine(0, true);
  }

  struct enum_wnd_context_t {
    std::set<DWORD> process_ids;
    bool requested_exit;
  };

  static BOOL CALLBACK
  prgrp_enum_windows(HWND hwnd, LPARAM lParam) {
    auto enum_ctx = (enum_wnd_context_t *) lParam;

    // Find the owner PID of this window
    DWORD wnd_process_id;
    if (!GetWindowThreadProcessId(hwnd, &wnd_process_id)) {
      // Continue enumeration
      return TRUE;
    }

    // Check if this window is owned by a process we want to terminate
    if (enum_ctx->process_ids.find(wnd_process_id) != enum_ctx->process_ids.end()) {
      // Send an async WM_CLOSE message to this window
      if (SendNotifyMessageW(hwnd, WM_CLOSE, 0, 0)) {
        BOOST_LOG(debug) << "Sent WM_CLOSE to PID: "sv << wnd_process_id;
        enum_ctx->requested_exit = true;
      }
      else {
        auto error = GetLastError();
        BOOST_LOG(warning) << "Failed to send WM_CLOSE to PID ["sv << wnd_process_id << "]: " << error;
      }
    }

    // Continue enumeration
    return TRUE;
  }

  /**
   * @brief Attempt to gracefully terminate a process group.
   * @param native_handle The job object handle.
   * @return true if termination was successfully requested.
   */
  bool
  request_process_group_exit(std::uintptr_t native_handle) {
    auto job_handle = (HANDLE) native_handle;

    // Get list of all processes in our job object
    bool success;
    DWORD required_length = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST);
    auto process_id_list = (PJOBOBJECT_BASIC_PROCESS_ID_LIST) calloc(1, required_length);
    auto fg = util::fail_guard([&process_id_list]() {
      free(process_id_list);
    });
    while (!(success = QueryInformationJobObject(job_handle, JobObjectBasicProcessIdList,
               process_id_list, required_length, &required_length)) &&
           GetLastError() == ERROR_MORE_DATA) {
      free(process_id_list);
      process_id_list = (PJOBOBJECT_BASIC_PROCESS_ID_LIST) calloc(1, required_length);
      if (!process_id_list) {
        return false;
      }
    }

    if (!success) {
      auto err = GetLastError();
      BOOST_LOG(warning) << "Failed to enumerate processes in group: "sv << err;
      return false;
    }
    else if (process_id_list->NumberOfProcessIdsInList == 0) {
      // If all processes are already dead, treat it as a success
      return true;
    }

    enum_wnd_context_t enum_ctx = {};
    enum_ctx.requested_exit = false;
    for (DWORD i = 0; i < process_id_list->NumberOfProcessIdsInList; i++) {
      enum_ctx.process_ids.emplace(process_id_list->ProcessIdList[i]);
    }

    // Enumerate all windows belonging to processes in the list
    EnumWindows(prgrp_enum_windows, (LPARAM) &enum_ctx);

    // Return success if we told at least one window to close
    return enum_ctx.requested_exit;
  }

  /**
   * @brief Checks if a process group still has running children.
   * @param native_handle The job object handle.
   * @return true if processes are still running.
   */
  bool
  process_group_running(std::uintptr_t native_handle) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting_info;

    if (!QueryInformationJobObject((HANDLE) native_handle, JobObjectBasicAccountingInformation, &accounting_info, sizeof(accounting_info), nullptr)) {
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to get job accounting info: "sv << err;
      return false;
    }

    return accounting_info.ActiveProcesses != 0;
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
    SOCKADDR_IN taddr_v4;
    SOCKADDR_IN6 taddr_v6;
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v6;
      msg.namelen = sizeof(taddr_v6);
    }
    else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v4;
      msg.namelen = sizeof(taddr_v4);
    }

    WSABUF buf;
    buf.buf = (char *) send_info.buffer;
    buf.len = send_info.block_size * send_info.block_count;

    msg.lpBuffers = &buf;
    msg.dwBufferCount = 1;
    msg.dwFlags = 0;

    // At most, one DWORD option and one PKTINFO option
    char cmbuf[WSA_CMSG_SPACE(sizeof(DWORD)) +
               std::max(WSA_CMSG_SPACE(sizeof(IN6_PKTINFO)), WSA_CMSG_SPACE(sizeof(IN_PKTINFO)))] = {};
    ULONG cmbuflen = 0;

    msg.Control.buf = cmbuf;
    msg.Control.len = sizeof(cmbuf);

    auto cm = WSA_CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      IN6_PKTINFO pktInfo;

      SOCKADDR_IN6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IPV6;
      cm->cmsg_type = IPV6_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }
    else {
      IN_PKTINFO pktInfo;

      SOCKADDR_IN saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_addr = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IP;
      cm->cmsg_type = IP_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }

    if (send_info.block_count > 1) {
      cmbuflen += WSA_CMSG_SPACE(sizeof(DWORD));

      cm = WSA_CMSG_NXTHDR(&msg, cm);
      cm->cmsg_level = IPPROTO_UDP;
      cm->cmsg_type = UDP_SEND_MSG_SIZE;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(DWORD));
      *((DWORD *) WSA_CMSG_DATA(cm)) = send_info.block_size;
    }

    msg.Control.len = cmbuflen;

    // If USO is not supported, this will fail and the caller will fall back to unbatched sends.
    DWORD bytes_sent;
    return WSASendMsg((SOCKET) send_info.native_socket, &msg, 1, &bytes_sent, nullptr, nullptr) != SOCKET_ERROR;
  }

  bool
  send(send_info_t &send_info) {
    WSAMSG msg;

    // Convert the target address into a SOCKADDR
    SOCKADDR_IN taddr_v4;
    SOCKADDR_IN6 taddr_v6;
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v6;
      msg.namelen = sizeof(taddr_v6);
    }
    else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v4;
      msg.namelen = sizeof(taddr_v4);
    }

    WSABUF buf;
    buf.buf = (char *) send_info.buffer;
    buf.len = send_info.size;

    msg.lpBuffers = &buf;
    msg.dwBufferCount = 1;
    msg.dwFlags = 0;

    char cmbuf[std::max(WSA_CMSG_SPACE(sizeof(IN6_PKTINFO)), WSA_CMSG_SPACE(sizeof(IN_PKTINFO)))] = {};
    ULONG cmbuflen = 0;

    msg.Control.buf = cmbuf;
    msg.Control.len = sizeof(cmbuf);

    auto cm = WSA_CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      IN6_PKTINFO pktInfo;

      SOCKADDR_IN6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IPV6;
      cm->cmsg_type = IPV6_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }
    else {
      IN_PKTINFO pktInfo;

      SOCKADDR_IN saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_addr = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IP;
      cm->cmsg_type = IP_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }

    msg.Control.len = cmbuflen;

    DWORD bytes_sent;
    if (WSASendMsg((SOCKET) send_info.native_socket, &msg, 1, &bytes_sent, nullptr, nullptr) == SOCKET_ERROR) {
      auto winerr = WSAGetLastError();
      BOOST_LOG(warning) << "WSASendMsg() failed: "sv << winerr;
      return false;
    }

    return true;
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
  int64_t
  qpc_counter() {
    LARGE_INTEGER performace_counter;
    if (QueryPerformanceCounter(&performace_counter)) return performace_counter.QuadPart;
    return 0;
  }

  std::chrono::nanoseconds
  qpc_time_difference(int64_t performance_counter1, int64_t performance_counter2) {
    auto get_frequency = []() {
      LARGE_INTEGER frequency;
      frequency.QuadPart = 0;
      QueryPerformanceFrequency(&frequency);
      return frequency.QuadPart;
    };
    static const double frequency = get_frequency();
    if (frequency) {
      return std::chrono::nanoseconds((int64_t) ((performance_counter1 - performance_counter2) * frequency / std::nano::den));
    }
    return {};
  }
}  // namespace platf
