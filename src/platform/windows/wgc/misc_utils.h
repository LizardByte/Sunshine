/**
 * @file src/platform/windows/wgc/misc_utils.h
 * @brief Minimal utility functions for WGC helper without heavy dependencies
 */

#pragma once

#include <cstdint>
#include <string>
#include <windows.h>
#include <avrt.h>
#include "src/utility.h"

namespace platf::dxgi {

  // Message structs for IPC Communication
  struct SharedHandleData {
    HANDLE textureHandle;
    UINT width;
    UINT height;
  };


  struct ConfigData {
    int dynamicRange;
    int log_level;
    wchar_t displayName[32];
  };

  // RAII wrappers for Windows resources
  struct safe_handle: public util::safe_ptr_v2<void, BOOL, CloseHandle> {
    using util::safe_ptr_v2<void, BOOL, CloseHandle>::safe_ptr_v2;

    explicit operator bool() const {
      auto handle = get();
      return handle != NULL && handle != INVALID_HANDLE_VALUE;
    }
  };

  struct memory_view_deleter {
    void operator()(void *ptr) {
      if (ptr) {
        UnmapViewOfFile(ptr);
      }
    }
  };

  using safe_memory_view = util::uniq_ptr<void, memory_view_deleter>;

  struct com_deleter {
    template<typename T>
    void operator()(T *ptr) {
      if (ptr) {
        ptr->Release();
      }
    }
  };

  template<typename T>
  using safe_com_ptr = util::uniq_ptr<T, com_deleter>;

  struct winevent_hook_deleter {
    void operator()(HWINEVENTHOOK hook) {
      if (hook) {
        UnhookWinEvent(hook);
      }
    }
  };

  using safe_winevent_hook = util::uniq_ptr<std::remove_pointer_t<HWINEVENTHOOK>, winevent_hook_deleter>;

  // RAII wrapper for MMCSS handles (AvSetMmThreadCharacteristicsW)
  struct mmcss_handle_deleter {
    void operator()(HANDLE handle) {
      if (handle) {
        AvRevertMmThreadCharacteristics(handle);
      }
    }
  };

  using safe_mmcss_handle = util::uniq_ptr<std::remove_pointer_t<HANDLE>, mmcss_handle_deleter>;
}  // namespace platf::dxgi

namespace platf::wgc {

  /**
   * @brief Check if a process with the given name is running.
   * @param processName The name of the process to check for.
   * @return `true` if the process is running, `false` otherwise.
   */
  bool is_process_running(const std::wstring &processName);

  /**
   * @brief Check if we're on the secure desktop (UAC prompt or login screen).
   * @return `true` if we're on the secure desktop, `false` otherwise.
   */
  bool is_secure_desktop_active();

  /**
   * @brief Check if the current process is running with system-level privileges.
   * @return `true` if the current process has system-level privileges, `false` otherwise.
   */
  bool is_running_as_system();

  /**
   * @brief Obtain the current sessions user's primary token with elevated privileges.
   * @param elevated Specify whether to elevate the process.
   * @return The user's token. If user has admin capability it will be elevated, otherwise it will be a limited token. On error, `nullptr`.
   */
  HANDLE retrieve_users_token(bool elevated);

  /**
   * @brief Get the parent process ID of the current process or a specific process.
   * @return The parent process ID.
   */
  DWORD get_parent_process_id();
  DWORD get_parent_process_id(DWORD process_id);

}  // namespace platf::wgc
