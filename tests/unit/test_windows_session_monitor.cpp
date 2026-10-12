/**
 * @file tests/unit/test_windows_session_monitor.cpp
 * @brief Exercise Windows session notifications through their native hidden window.
 */
#include "../tests_common.h"

#ifdef _WIN32
  #include <atomic>
  #include <csignal>
  #include <src/platform/windows/session_monitor.h>
  #include <src/utility.h>
  #include <thread>

/**
 * @brief Native creation and shutdown messages retain the owning controller and close only their window.
 */
TEST(WindowsSessionMonitorTest, NativeMessagesRestoreControllerAndCloseWindow) {
  lifetime::controller_t controller;
  std::atomic_int signals {};
  lifetime::signal_guard_t guard {controller};
  guard.on_signal(SIGINT, [&signals]() {
    ++signals;
  });

  // Keep WM_QUIT on the dedicated monitor thread, as the application does.
  std::jthread monitor([&controller, &signals]() {
    WNDCLASSA window_class {};
    window_class.lpszClassName = "SunshineSessionMonitorRegression";
    window_class.lpfnWndProc = platf::session_monitor_window_proc;
    ASSERT_NE(RegisterClassA(&window_class), 0);
    const auto unregister = util::fail_guard([&window_class]() {
      UnregisterClassA(window_class.lpszClassName, nullptr);
    });

    const auto window = CreateWindowExA(0, window_class.lpszClassName, "Session monitor regression", 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, &controller);
    ASSERT_NE(window, nullptr);
    const auto close_window = util::fail_guard([window]() {
      if (IsWindow(window)) {
        DestroyWindow(window);
      }
    });
    EXPECT_EQ(GetWindowLongPtrA(window, GWLP_USERDATA), reinterpret_cast<LONG_PTR>(&controller));
    EXPECT_EQ(SendMessageA(window, WM_ENDSESSION, TRUE, 0), 0);
    EXPECT_EQ(SendMessageA(window, WM_ENDSESSION, TRUE, 0), 0);
    EXPECT_EQ(signals.load(), 2);

    SetWindowLongPtrA(window, GWLP_USERDATA, 0);
    EXPECT_EQ(SendMessageA(window, WM_ENDSESSION, TRUE, 0), 0);
    EXPECT_EQ(signals.load(), 2);
    EXPECT_EQ(SendMessageA(window, WM_CLOSE, 0, 0), 0);
    EXPECT_FALSE(IsWindow(window));
    MSG quit {};
    EXPECT_TRUE(PeekMessageA(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    EXPECT_EQ(quit.wParam, 0);
  });
  monitor.join();
  EXPECT_EQ(signals.load(), 2);
}
#endif
