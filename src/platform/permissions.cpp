/**
 * @file src/platform/permissions.cpp
 * @brief Shared permission policy and file access checks.
 */
// header include
#include "permissions.h"

// standard includes
#include <algorithm>

// platform includes
#ifdef _WIN32
  #include <Windows.h>
#else
  #include <unistd.h>
#endif

namespace platf {
  bool required_permissions_granted(const std::vector<permission_status_t> &permissions) {
    return std::ranges::all_of(permissions, [](const auto &permission) {
      return !permission.required || !permission.verifiable || permission.status == "granted";
    });
  }

  bool can_access_directory(const std::filesystem::path &path) {
#ifdef _WIN32
    // Request directory rights from the effective token; filesystem::perms does
    // not reflect Windows ACLs or deny entries.
    HANDLE handle = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_ADD_FILE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      return false;
    }
    CloseHandle(handle);
    return true;
#else
    std::error_code error;
    return std::filesystem::is_directory(path, error) && !error && access(path.c_str(), R_OK | W_OK | X_OK) == 0;
#endif
  }
}  // namespace platf
