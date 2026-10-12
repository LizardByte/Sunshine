/**
 * @file src/platform/windows/session_monitor.h
 * @brief Windows session notifications routed to their application lifetime.
 */
#pragma once

// clang-format off
#include <WinSock2.h>
#include <Windows.h>
// clang-format on
#include "src/entry_handler.h"

#include <bit>
#include <iostream>

namespace platf {
  /**
   * @brief Handle session-change messages for the application's hidden monitor window.
   * @details Windows transports CREATESTRUCT and user-data pointer bits through pointer-sized integer slots.
   * The representation copies retain those native pointer values without changing their types or ownership.
   *
   * @param hwnd Window receiving the notification.
   * @param message Native window message.
   * @param wparam Message-specific value.
   * @param lparam Message-specific value, including the creation structure for WM_NCCREATE.
   * @return Native message result.
   */
  inline LRESULT CALLBACK session_monitor_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
      const auto create = std::bit_cast<const CREATESTRUCTA *>(lparam);
      SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    switch (message) {
      case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      case WM_ENDSESSION:
        std::cout << "Received WM_ENDSESSION" << std::endl;
        if (const auto controller = std::bit_cast<lifetime::controller_t *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA))) {
          controller->exit_sunshine(0, true);
        }
        return 0;
      default:
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
  }
}  // namespace platf
