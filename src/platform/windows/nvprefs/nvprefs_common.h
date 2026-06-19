/**
 * @file src/platform/windows/nvprefs/nvprefs_common.h
 * @brief Declarations for common nvidia preferences.
 */
#pragma once

// platform includes
// disable clang-format header reordering
// clang-format off
#include <Windows.h>
#include <AclAPI.h>
// clang-format on

// local includes
#include "src/utility.h"

namespace nvprefs {

  struct safe_handle: public util::safe_ptr_v2<void, BOOL, CloseHandle> {
    using util::safe_ptr_v2<void, BOOL, CloseHandle>::safe_ptr_v2;

    explicit operator bool() const {
      auto handle = get();
      return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }
  };

  struct safe_hlocal_deleter {
    void operator()(void *p) {
      LocalFree(p);
    }
  };

  template<typename T>
  using safe_hlocal = util::uniq_ptr<std::remove_pointer_t<T>, safe_hlocal_deleter>;

  using safe_sid = util::safe_ptr_v2<void, PVOID, FreeSid>;

  void info_message(const std::wstring &message);

  void info_message(const std::string &message);

  void error_message(const std::wstring &message);

  void error_message(const std::string &message);

  struct nvprefs_options {
    bool opengl_vulkan_on_dxgi = true;
    bool sunshine_high_power_mode = true;
  };

  nvprefs_options get_nvprefs_options();

}  // namespace nvprefs
