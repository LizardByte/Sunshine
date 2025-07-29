/**
 * @file src/platform/windows/ipc/misc_utils.h
 * @brief Minimal utility functions for WGC helper without heavy dependencies
 */

#pragma once

#include "src/utility.h"

#include <cstdint>
#include <string>
#include <windows.h>
#include <avrt.h>

namespace platf::dxgi {

  using safe_token = util::safe_ptr_v2<void, BOOL, &CloseHandle>;
  using safe_sid = util::safe_ptr_v2<void, PVOID, &FreeSid>;
  using safe_local_mem = util::safe_ptr_v2<void, HLOCAL, &LocalFree>;

  /**
   * @brief RAII wrapper for managing OVERLAPPED I/O context with event handle.
   *
   * - Initializes an OVERLAPPED structure and creates an event for asynchronous I/O.
   *
   * - Provides move semantics for safe transfer of ownership.
   *
   * - Cleans up the event handle on destruction.
   *
   * - Offers access to the underlying OVERLAPPED pointer and event handle.
   *
   * - Allows checking if the context is valid.
   */
  class io_context {
  public:
    /**
     * @brief Constructs an io_context, initializing OVERLAPPED and creating an event.
     */
    io_context();

    /**
     * @brief Destroys the io_context, closing the event handle.
     */
    ~io_context();

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    /**
     * @brief Move constructor for io_context.
     * @param other The io_context to move from.
     */
    io_context(io_context &&other) noexcept;

    /**
     * @brief Move assignment operator for io_context.
     * @param other The io_context to move from.
     * @return Reference to this io_context.
     */
    io_context &operator=(io_context &&other) noexcept;

    /**
     * @brief Get a pointer to the underlying OVERLAPPED structure.
     * @return Pointer to OVERLAPPED.
     */
    OVERLAPPED *get();

    /**
     * @brief Get the event handle associated with the OVERLAPPED structure.
     * @return The event HANDLE.
     */
    HANDLE event() const;

    /**
     * @brief Check if the io_context is valid (event handle is valid).
     * @return true if valid, false otherwise.
     */
    bool is_valid() const;

  private:
    OVERLAPPED _ovl;
    HANDLE _event;
  };

  /**
   * @brief Specialized RAII wrapper for DACL (Discretionary Access Control List).
   *
   * - Manages a PACL pointer, ensuring proper cleanup.
   *
   * - Provides move semantics for safe transfer of ownership.
   *
   * - Disallows copy semantics.
   *
   * - Allows resetting, accessing, and releasing the underlying PACL.
   *
   * - Supports boolean conversion to check validity.
   */
  struct safe_dacl {
    PACL dacl = nullptr;

    /**
     * @brief Constructs an empty safe_dacl.
     */
    safe_dacl();

    /**
     * @brief Constructs a safe_dacl from a given PACL.
     * @param p The PACL to manage.
     */
    explicit safe_dacl(PACL p);

    /**
     * @brief Destroys the safe_dacl, freeing the PACL if owned.
     */
    ~safe_dacl();

    /**
     * @brief Move constructor for safe_dacl.
     * @param other The safe_dacl to move from.
     */
    safe_dacl(safe_dacl &&other) noexcept;

    /**
     * @brief Move assignment operator for safe_dacl.
     * @param other The safe_dacl to move from.
     * @return Reference to this safe_dacl.
     */
    safe_dacl &operator=(safe_dacl &&other) noexcept;

    safe_dacl(const safe_dacl &) = delete;
    safe_dacl &operator=(const safe_dacl &) = delete;

    /**
     * @brief Reset the managed PACL, freeing the previous one if owned.
     * @param p The new PACL to manage (default nullptr).
     */
    void reset(PACL p = nullptr);

    /**
     * @brief Get the underlying PACL pointer.
     * @return The PACL pointer.
     */
    PACL get() const;

    /**
     * @brief Release ownership of the PACL and return it.
     * @return The PACL pointer.
     */
    PACL release();

    /**
     * @brief Check if the safe_dacl holds a valid PACL.
     * @return true if valid, false otherwise.
     */
    explicit operator bool() const;
  };

  /**
   * @brief Structure for sharing handle and texture metadata via IPC.
   * @param texture_handle Shared texture handle.
   * @param width Width of the texture.
   * @param height Height of the texture.
   */
  struct shared_handle_data_t {
    HANDLE texture_handle;
    UINT width;
    UINT height;
  };

  /**
   * @brief Structure for configuration data shared via IPC.
   * @param dynamic_range Dynamic range setting.
   * @param log_level Logging level.
   * @param display_name Display name (wide string, max 32 chars).
   */
  struct config_data_t {
    int dynamic_range;
    int log_level;
    wchar_t display_name[32];
  };

  /**
   * @brief RAII wrapper for Windows HANDLE resources.
   *
   * - Inherits from util::safe_ptr_v2 for automatic handle management.
   *
   * - Provides boolean conversion to check handle validity.
   */
  struct safe_handle: public util::safe_ptr_v2<void, BOOL, CloseHandle> {
    using util::safe_ptr_v2<void, BOOL, CloseHandle>::safe_ptr_v2;

    /**
     * @brief Check if the handle is valid (not NULL or INVALID_HANDLE_VALUE).
     * @return true if valid, false otherwise.
     */
    explicit operator bool() const {
      auto handle = get();
      return handle != NULL && handle != INVALID_HANDLE_VALUE;
    }
  };

  /**
   * @brief Deleter for memory views mapped with MapViewOfFile.
   */
  struct memory_view_deleter {
    /**
     * @brief Unmaps the memory view if pointer is not null.
     * @param ptr Pointer to the mapped memory.
     */
    void operator()(void *ptr) {
      if (ptr) {
        UnmapViewOfFile(ptr);
      }
    }
  };

  /**
   * @brief Unique pointer type for memory views mapped with MapViewOfFile.
   */
  using safe_memory_view = util::uniq_ptr<void, memory_view_deleter>;

  /**
   * @brief Deleter for COM objects, calls Release on the pointer.
   */
  struct com_deleter {
    /**
     * @brief Releases the COM object if pointer is not null.
     * @tparam T COM object type.
     * @param ptr Pointer to the COM object.
     */
    template<typename T>
    void operator()(T *ptr) {
      if (ptr) {
        ptr->Release();
      }
    }
  };

  /**
   * @brief Unique pointer type for COM objects with automatic Release.
   * @tparam T COM object type.
   */
  template<typename T>
  using safe_com_ptr = util::uniq_ptr<T, com_deleter>;

  /**
   * @brief Deleter for Windows event hooks, calls UnhookWinEvent.
   */
  struct winevent_hook_deleter {
    /**
     * @brief Unhooks the event if hook is not null.
     * @param hook The event hook handle.
     */
    void operator()(HWINEVENTHOOK hook) {
      if (hook) {
        UnhookWinEvent(hook);
      }
    }
  };

  /**
   * @brief Unique pointer type for Windows event hooks.
   */
  using safe_winevent_hook = util::uniq_ptr<std::remove_pointer_t<HWINEVENTHOOK>, winevent_hook_deleter>;

  /**
   * @brief Deleter for MMCSS handles, calls AvRevertMmThreadCharacteristics.
   */
  struct mmcss_handle_deleter {
    /**
     * @brief Reverts MMCSS thread characteristics if handle is not null.
     * @param handle The MMCSS handle.
     */
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

  /**
   * @brief Get the parent process ID for a specified process.
   *
   * Steps:
   *
   * - Takes a process ID as input.
   *
   * - Queries the system for the parent process of the given process ID.
   *
   * - Returns the parent process ID, or 0 if not found or on error.
   *
   * @param process_id The process ID for which to retrieve the parent process ID.
   * @return The parent process ID, or 0 if not found or on error.
   */
  DWORD get_parent_process_id(DWORD process_id);

}  // namespace platf::dxgi
