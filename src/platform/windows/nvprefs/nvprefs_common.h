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

  /**
   * @brief Owning Windows HANDLE wrapper that closes handles automatically.
   */
  struct safe_handle: public util::safe_ptr_v2<void, BOOL, CloseHandle> {
    using util::safe_ptr_v2<void, BOOL, CloseHandle>::safe_ptr_v2;

    /**
     * @brief Check whether the wrapped Windows handle is valid.
     */
    explicit operator bool() const {
      auto handle = get();
      return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }
  };

  /**
   * @brief Deleter for memory allocated by Windows LocalAlloc APIs.
   */
  struct safe_hlocal_deleter {
    /**
     * @brief Free memory allocated by Windows local-memory APIs.
     *
     * @param p LocalAlloc-compatible pointer to release.
     */
    void operator()(void *p) {
      LocalFree(p);
    }
  };

  /**
   * @brief Unique pointer for memory released with `LocalFree`.
   */
  template<typename T>
  using safe_hlocal = util::uniq_ptr<std::remove_pointer_t<T>, safe_hlocal_deleter>;

  /**
   * @brief Safe pointer for Windows security identifiers released with `FreeSid`.
   */
  using safe_sid = util::safe_ptr_v2<void, PVOID, FreeSid>;

  /**
   * @brief Forward an informational NVPrefs message to the logger.
   *
   * @param message Message text to log or report.
   */
  void info_message(const std::wstring &message);

  /**
   * @brief Forward an informational NVPrefs message to the logger.
   *
   * @param message Message text to log or report.
   */
  void info_message(const std::string &message);

  /**
   * @brief Forward an NVPrefs error message to the logger.
   *
   * @param message Message text to log or report.
   */
  void error_message(const std::wstring &message);

  /**
   * @brief Forward an NVPrefs error message to the logger.
   *
   * @param message Message text to log or report.
   */
  void error_message(const std::string &message);

  /**
   * @brief Parsed command-line options for the NVIDIA preferences helper.
   */
  struct nvprefs_options {
    bool opengl_vulkan_on_dxgi = true;  ///< Whether NVIDIA OpenGL/Vulkan-on-DXGI should be enabled.
    bool sunshine_high_power_mode = true;  ///< Whether NVIDIA high-power mode should be enabled for Sunshine.
  };

  /**
   * @brief Get nvprefs options.
   *
   * @return Parsed command-line options for NVIDIA profile preference handling.
   */
  nvprefs_options get_nvprefs_options();

}  // namespace nvprefs
