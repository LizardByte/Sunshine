/**
 * @file tests/unit/platform/windows/test_misc.cpp
 * @brief Tests for Windows thread priority mapping.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <array>
  #include <utility>

  // platform includes
  #include <Windows.h>

  // local includes
  #include <src/platform/common.h>
  #include <src/utility.h>

TEST(WindowsThreadPriorityTest, MapsEveryPriorityAndIgnoresUnknownValues) {
  using enum platf::thread_priority_e;
  const auto thread = GetCurrentThread();
  const auto original_priority = GetThreadPriority(thread);
  ASSERT_NE(original_priority, THREAD_PRIORITY_ERROR_RETURN);
  const auto restore_priority = util::fail_guard([thread, original_priority]() {
    EXPECT_TRUE(SetThreadPriority(thread, original_priority));
  });
  const std::array cases {
    std::pair {low, THREAD_PRIORITY_BELOW_NORMAL},
    std::pair {normal, THREAD_PRIORITY_NORMAL},
    std::pair {high, THREAD_PRIORITY_ABOVE_NORMAL},
    std::pair {critical, THREAD_PRIORITY_HIGHEST},
  };
  for (const auto &[priority, expected] : cases) {
    platf::adjust_thread_priority(priority);
    EXPECT_EQ(GetThreadPriority(thread), expected);
  }
  platf::adjust_thread_priority(static_cast<platf::thread_priority_e>(-1));
  EXPECT_EQ(GetThreadPriority(thread), THREAD_PRIORITY_HIGHEST);
}
#endif
