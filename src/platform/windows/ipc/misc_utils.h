/**
 * @file src/platform/windows/ipc/misc_utils.h
 * @brief Minimal utility functions for WGC helper without heavy dependencies
 */

#pragma once


#include <cstdint>
#include <string>
#include <windows.h>
#include <avrt.h>
#include "src/utility.h"

namespace platf::dxgi {

  // RAII wrappers for Windows security objects
  using safe_token = util::safe_ptr_v2<void, BOOL, &CloseHandle>;
  using safe_sid = util::safe_ptr_v2<void, PVOID, &FreeSid>;
  using safe_local_mem = util::safe_ptr_v2<void, HLOCAL, &LocalFree>;

  // RAII helper for overlapped I/O operations
  class io_context {
  public:
    io_context();
    ~io_context();

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    io_context(io_context &&other) noexcept;
    io_context &operator=(io_context &&other) noexcept;

    OVERLAPPED *get();
    HANDLE event() const;
    bool is_valid() const;

  private:
    OVERLAPPED _ovl;
    HANDLE _event;
  };

  // Specialized wrapper for DACL since it needs to be cast to PACL
  struct safe_dacl {
    PACL dacl = nullptr;

    safe_dacl();
    explicit safe_dacl(PACL p);
    ~safe_dacl();

    safe_dacl(safe_dacl &&other) noexcept;
    safe_dacl &operator=(safe_dacl &&other) noexcept;

    safe_dacl(const safe_dacl &) = delete;
    safe_dacl &operator=(const safe_dacl &) = delete;

    void reset(PACL p = nullptr);
    PACL get() const;
    PACL release();
    explicit operator bool() const;
  };

  // Message structs for IPC Communication
  struct shared_handle_data_t {
    HANDLE texture_handle;
    UINT width;
    UINT height;
  };

  struct config_data_t {
    int dynamic_range;
    int log_level;
    wchar_t display_name[32];
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

}  // namespace platf::dxgi
