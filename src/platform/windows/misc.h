/**
 * @file src/platform/windows/misc.h
 * @brief Miscellaneous declarations for Windows.
 */
#pragma once

// standard includes
#include <chrono>
#include <string_view>

// platform includes
#include <windows.h>
#include <winnt.h>

namespace platf {
  void print_status(const std::string_view &prefix, HRESULT status);
  HDESK syncThreadDesktop();

  int64_t qpc_counter();

  std::chrono::nanoseconds qpc_time_difference(int64_t performance_counter1, int64_t performance_counter2);

  /**
   * @brief Convert a UTF-8 string into a UTF-16 wide string.
   * @param string The UTF-8 string.
   * @return The converted UTF-16 wide string.
   */
  std::wstring from_utf8(const std::string &string);

  /**
   * @brief Convert a UTF-16 wide string into a UTF-8 string.
   * @param string The UTF-16 wide string.
   * @return The converted UTF-8 string.
   */
  std::string to_utf8(const std::wstring &string);

  /**
   * @brief Checks if the current process is running in a streaming context.
   * @details This function determines whether the application is currently operating
   * in a streaming mode, which may affect how certain platform-specific features behave.
   * It is typically used to adjust behavior or resource usage when streaming is active.
   * @return true if streaming is active, false otherwise.
   */
  bool is_streaming();

  /**
   * @brief Notifies the platform layer that streaming has started.
   * @details This function should be called when the application begins streaming.
   * It allows the platform to perform any necessary setup or optimizations for streaming mode,
   * such as adjusting resource allocation or enabling streaming-specific features.
   */
  void streaming_will_start();

  /**
   * @brief Notifies the platform layer that streaming will stop.
   * @details This function should be called before the application stops streaming.
   * It allows the platform to perform any necessary cleanup or revert changes made for streaming mode,
   * ensuring the application returns to its normal operating state.
   */
  void streaming_will_stop();

  /**
   * @brief Checks if a mouse is physically present and, if not, toggles Mouse Keys
   * to force the software cursor to become visible.
   * @details This is used as a workaround for KVM switches that do not send
   * standard device removal messages, which can cause the cursor to disappear.
   */
  void check_and_force_cursor_visibility();

}  // namespace platf
