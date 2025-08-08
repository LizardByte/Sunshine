/**
 * @file src/platform/windows/ipc/misc_utils.h
 * @brief Utility declarations for WGC helper.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string>

// local includes
#include "src/utility.h"

// platform includes
#ifdef _WIN32
  #include <windows.h>
#endif
#include <avrt.h>
#include <tlhelp32.h>

namespace platf::dxgi {

  using safe_token = util::safe_ptr_v2<void, BOOL, &CloseHandle>;
  using safe_sid = util::safe_ptr_v2<void, PVOID, &FreeSid>;
  using safe_local_mem = util::safe_ptr_v2<void, HLOCAL, &LocalFree>;

  /**
   * @brief RAII wrapper for OVERLAPPED asynchronous I/O with an auto-created event.
   */
  class io_context {
  public:
    /**
     * @brief Construct and initialize OVERLAPPED and its event.
     */
    io_context();

    /**
     * @brief Destroy context and close the event handle.
     */
    ~io_context();

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    /**
     * @brief Move construct from another context.
     * @param other Source context.
     */
    io_context(io_context &&other) noexcept;

    /**
     * @brief Move-assign from another context.
     * @param other Source context.
     * @return Reference to this instance.
     */
    io_context &operator=(io_context &&other) noexcept;

    /**
     * @brief Get pointer to underlying OVERLAPPED.
     * @return Pointer to the internal OVERLAPPED.
     */
    OVERLAPPED *get();

    /**
     * @brief Get event handle associated with the OVERLAPPED.
     * @return Event `HANDLE`.
     */
    HANDLE event() const;

    /**
     * @brief Check validity of the context (event successfully created).
     * @return `true` if valid, otherwise `false`.
     */
    bool is_valid() const;

  private:
    OVERLAPPED _ovl;
    HANDLE _event;
  };

  /**
   * @brief RAII wrapper for a DACL (PACL) allocated by Windows APIs.
   */
  struct safe_dacl {
    PACL _dacl = nullptr;

    /**
     * @brief Default construct empty wrapper.
     */
    safe_dacl();

    /**
     * @brief Take ownership of an existing PACL.
     * @param p PACL to manage.
     */
    explicit safe_dacl(PACL p);

    /**
     * @brief Free the owned PACL (if any).
     */
    ~safe_dacl();

    /**
     * @brief Move construct, transferring ownership.
     * @param other Source.
     */
    safe_dacl(safe_dacl &&other) noexcept;

    /**
     * @brief Move-assign, releasing any currently owned PACL.
     * @param other Source.
     * @return Reference to this instance.
     */
    safe_dacl &operator=(safe_dacl &&other) noexcept;

    safe_dacl(const safe_dacl &) = delete;
    safe_dacl &operator=(const safe_dacl &) = delete;

    /**
     * @brief Replace the managed PACL.
     * @param p New PACL (or `nullptr`).
     */
    void reset(PACL p = nullptr);

    /**
     * @brief Get raw PACL pointer.
     * @return Managed PACL pointer.
     */
    PACL get() const;

    /**
     * @brief Release ownership without freeing.
     * @return Released PACL pointer.
     */
    PACL release();

    /**
     * @brief Test whether a PACL is owned.
     * @return `true` if a PACL is set, else `false`.
     */
    explicit operator bool() const;
  };

  /**
   * @brief RAII wrapper for generic Windows `HANDLE` values.
   */
  struct safe_handle: public util::safe_ptr_v2<void, BOOL, CloseHandle> {
    using util::safe_ptr_v2<void, BOOL, CloseHandle>::safe_ptr_v2;

    /**
     * @brief Check handle validity (not `NULL` or `INVALID_HANDLE_VALUE`).
     * @return `true` if valid, else `false`.
     */
    explicit operator bool() const {
      auto handle = get();
      return handle != NULL && handle != INVALID_HANDLE_VALUE;
    }
  };

  /**
   * @brief Deleter for views returned by `MapViewOfFile`.
   */
  struct memory_view_deleter {
    /**
     * @brief Unmap a mapped view if non-null.
     * @param ptr Base address returned by `MapViewOfFile`.
     */
    void operator()(void *ptr) {
      if (ptr) {
        UnmapViewOfFile(ptr);
      }
    }
  };

  /**
   * @brief Unique pointer for a mapped file view.
   */
  using safe_memory_view = util::uniq_ptr<void, memory_view_deleter>;

  /**
   * @brief COM object deleter calling `Release()`.
   */
  struct com_deleter {
    /**
     * @brief Release COM object if non-null.
     * @tparam T COM interface type.
     * @param ptr Interface pointer.
     */
    template<typename T>
    void operator()(T *ptr) {
      if (ptr) {
        ptr->Release();
      }
    }
  };

  /**
   * @brief Unique pointer alias for COM interfaces.
   * @tparam T COM interface type.
   */
  template<typename T>
  using safe_com_ptr = util::uniq_ptr<T, com_deleter>;

  /**
   * @brief Deleter for WinEvent hooks (UnhookWinEvent).
   */
  struct winevent_hook_deleter {
    /**
     * @brief Unhook if handle is non-null.
     * @param hook WinEvent hook handle.
     */
    void operator()(HWINEVENTHOOK hook) {
      if (hook) {
        UnhookWinEvent(hook);
      }
    }
  };

  /**
   * @brief Unique pointer for WinEvent hook handles.
   */
  using safe_winevent_hook = util::uniq_ptr<std::remove_pointer_t<HWINEVENTHOOK>, winevent_hook_deleter>;

  /**
   * @brief Deleter reverting MMCSS characteristics.
   */
  struct mmcss_handle_deleter {
    /**
     * @brief Revert MMCSS thread characteristics if handle present.
     * @param handle MMCSS task handle.
     */
    void operator()(HANDLE handle) {
      if (handle) {
        AvRevertMmThreadCharacteristics(handle);
      }
    }
  };

  using safe_mmcss_handle = util::uniq_ptr<std::remove_pointer_t<HANDLE>, mmcss_handle_deleter>;

  /**
   * @brief Check if a process with the specified executable name is running.
   * @param process_name Executable name (case-insensitive).
   * @return `true` if at least one matching process exists, else `false`.
   */
  bool is_process_running(const std::wstring &process_name);

  /**
   * @brief Determine whether the secure (e.g. UAC / logon) desktop is active.
   * @return `true` if the secure desktop is active, else `false`.
   */
  bool is_secure_desktop_active();

  /**
   * @brief Check if the current process token belongs to Local System.
   * @return `true` if running as SYSTEM, else `false`.
   */
  bool is_running_as_system();

  /**
   * @brief Retrieve the active session user's primary token (optionally elevated).
   * @param elevated Request elevated token when available.
   * @return User token `HANDLE`, or `nullptr` on failure.
   */
  HANDLE retrieve_users_token(bool elevated);

  /**
   * @brief Generate a random GUID in string form (RFC 4122 style).
   * @return UTF-8 GUID string or empty on failure.
   */
  std::string generate_guid();

  /**
   * @brief Convert UTF-8 string to wide string (UTF-16 on Windows).
   * @param utf8_str Source UTF-8 string.
   * @return Resulting wide string.
   */
  std::wstring utf8_to_wide(const std::string &utf8_str);

  /**
   * @brief Convert wide string (UTF-16) to UTF-8.
   * @param wide_str Source wide string.
   * @return Resulting UTF-8 string.
   */
  std::string wide_to_utf8(const std::wstring &wide_str);

}  // namespace platf::dxgi
