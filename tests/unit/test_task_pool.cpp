/**
 * @file tests/unit/test_task_pool.cpp
 * @brief Regression coverage for delayed task lookup over the complete queue.
 */

// test includes
#include "../tests_common.h"

// local includes
#include <src/task_pool.h>

TEST(TaskPoolTest, FindsRequestedDelayedTaskAndRejectsMissingIdentifiers) {
  using namespace std::chrono_literals;
  task_pool_util::TaskPool pool;
  EXPECT_FALSE(pool.pop(nullptr).has_value());

  auto first = pool.pushDelayed([]() {
    return 11;
  },
                                1h);
  auto second = pool.pushDelayed([]() {
    return 22;
  },
                                 2h);
  EXPECT_FALSE(pool.pop(nullptr).has_value());

  auto removed = pool.pop(first.task_id);
  ASSERT_TRUE(removed.has_value());
  ASSERT_NE(removed->second, nullptr);
  removed->second->run();
  EXPECT_EQ(first.future.get(), 11);

  auto other = pool.pop(second.task_id);
  ASSERT_TRUE(other.has_value());
  ASSERT_NE(other->second, nullptr);
  other->second->run();
  EXPECT_EQ(second.future.get(), 22);
}
