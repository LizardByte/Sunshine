#pragma once

// sunshine utility header for generic smart pointers
#include "src/utility.h"

// sunshine boost::log severity levels
#include "src/main.h"

// standard library headers
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

// winapi headers
// disable clang-format header reordering
// clang-format off
#include <windows.h>
#include <aclapi.h>
// clang-format on

// nvapi headers
// disable clang-format header reordering
// clang-format off
#include <nvapi.h>
#include <NvApiDriverSettings.h>
// clang-format on

// boost headers
#include <boost/json.hpp>

namespace nvprefs {

  struct safe_handle: public util::safe_ptr_v2<void, BOOL, CloseHandle> {
    using util::safe_ptr_v2<void, BOOL, CloseHandle>::safe_ptr_v2;
    explicit operator bool() const {
      auto handle = get();
      return handle != NULL && handle != INVALID_HANDLE_VALUE;
    }
  };

  struct safe_hlocal_deleter {
    void
    operator()(void *p) {
      LocalFree(p);
    }
  };

  template <typename T>
  using safe_hlocal = util::uniq_ptr<std::remove_pointer_t<T>, safe_hlocal_deleter>;

  using safe_sid = util::safe_ptr_v2<void, PVOID, FreeSid>;

  void
  info_message(const std::wstring &message);

  void
  info_message(const std::string &message);

  void
  error_message(const std::wstring &message);

  void
  error_message(const std::string &message);

}  // namespace nvprefs
