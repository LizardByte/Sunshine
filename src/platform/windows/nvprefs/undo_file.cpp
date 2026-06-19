/**
 * @file src/platform/windows/nvprefs/undo_file.cpp
 * @brief Definitions for the nvidia undo file.
 */
// local includes
#include "undo_file.h"

namespace {

  using namespace nvprefs;

  DWORD relax_permissions(HANDLE file_handle) {
    PACL old_dacl = nullptr;

    safe_hlocal<PSECURITY_DESCRIPTOR> sd;
    DWORD status = GetSecurityInfo(file_handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd);
    if (status != ERROR_SUCCESS) {
      return status;
    }

    safe_sid users_sid;
    SID_IDENTIFIER_AUTHORITY nt_authorithy = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&nt_authorithy, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &users_sid)) {
      return GetLastError();
    }

    EXPLICIT_ACCESS ea = {};
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE | DELETE;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.ptstrName = (LPTSTR) users_sid.get();

    safe_hlocal<PACL> new_dacl;
    status = SetEntriesInAcl(1, &ea, old_dacl, &new_dacl);
    if (status != ERROR_SUCCESS) {
      return status;
    }

    status = SetSecurityInfo(file_handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl.get(), nullptr);
    if (status != ERROR_SUCCESS) {
      return status;
    }

    return 0;
  }

}  // namespace

namespace nvprefs {

  std::optional<undo_file_t> undo_file_t::open_existing_file(std::filesystem::path file_path, bool &access_denied) {
    undo_file_t file;
    file.file_handle.reset(CreateFileW(file_path.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.file_handle) {
      access_denied = false;
      return file;
    } else {
      auto last_error = GetLastError();
      access_denied = (last_error != ERROR_FILE_NOT_FOUND && last_error != ERROR_PATH_NOT_FOUND);
      return std::nullopt;
    }
  }

  std::optional<undo_file_t> undo_file_t::create_new_file(std::filesystem::path file_path) {
    undo_file_t file;
    file.file_handle.reset(CreateFileW(file_path.c_str(), GENERIC_WRITE | STANDARD_RIGHTS_ALL, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));

    if (file.file_handle) {
      // give GENERIC_READ, GENERIC_WRITE and DELETE permissions to Users group
      if (relax_permissions(file.file_handle.get()) != 0) {
        error_message("Failed to relax permissions on undo file");
      }
      return file;
    } else {
      return std::nullopt;
    }
  }

  bool undo_file_t::delete_file() {
    if (!file_handle) {
      return false;
    }

    FILE_DISPOSITION_INFO delete_file_info = {TRUE};
    if (SetFileInformationByHandle(file_handle.get(), FileDispositionInfo, &delete_file_info, sizeof(delete_file_info))) {
      file_handle.reset();
      return true;
    } else {
      return false;
    }
  }

  bool undo_file_t::write_undo_data(const undo_data_t &undo_data) {
    if (!file_handle) {
      return false;
    }

    std::string buffer;
    try {
      buffer = undo_data.write();
    } catch (...) {
      error_message("Couldn't serialize undo data");
      return false;
    }

    if (!SetFilePointerEx(file_handle.get(), {}, nullptr, FILE_BEGIN) || !SetEndOfFile(file_handle.get())) {
      error_message("Couldn't clear undo file");
      return false;
    }

    DWORD bytes_written = 0;
    if (!WriteFile(file_handle.get(), buffer.data(), buffer.size(), &bytes_written, nullptr) || bytes_written != buffer.size()) {
      error_message("Couldn't write undo file");
      return false;
    }

    if (!FlushFileBuffers(file_handle.get())) {
      error_message("Failed to flush undo file");
    }

    return true;
  }

  std::optional<undo_data_t> undo_file_t::read_undo_data() {
    if (!file_handle) {
      return std::nullopt;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle.get(), &file_size)) {
      error_message("Couldn't get undo file size");
      return std::nullopt;
    }

    if ((size_t) file_size.QuadPart > 1024) {
      error_message("Undo file size is unexpectedly large, aborting");
      return std::nullopt;
    }

    std::vector<char> buffer(file_size.QuadPart);
    DWORD bytes_read = 0;
    if (!ReadFile(file_handle.get(), buffer.data(), buffer.size(), &bytes_read, nullptr) || bytes_read != buffer.size()) {
      error_message("Couldn't read undo file");
      return std::nullopt;
    }

    undo_data_t undo_data;
    try {
      undo_data.read(buffer);
    } catch (...) {
      error_message("Couldn't parse undo file");
      return std::nullopt;
    }
    return undo_data;
  }

}  // namespace nvprefs
