/**
 * @file tests/unit/test_thread_safe.cpp
 * @brief Tests for thread-safe utility types.
 */

// standard includes
#include <cstdint>
#include <type_traits>

// lib includes
#include <gtest/gtest.h>

// local includes
#include "src/thread_safe.h"

static_assert(!std::is_convertible_v<std::uint32_t, safe::queue_t<int>>);

TEST(ThreadSafeQueue, RejectsNewestItemAtCapacity) {
  safe::queue_t<int> queue {1, safe::queue_t<int>::overflow_policy_e::reject};

  ASSERT_TRUE(queue.raise(1));
  EXPECT_FALSE(queue.raise(2));

  const auto value = queue.pop();
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 1);
}

TEST(ThreadSafeQueue, DropsQueuedItemsByDefaultAtCapacity) {
  safe::queue_t<int> queue {2};

  ASSERT_TRUE(queue.raise(1));
  ASSERT_TRUE(queue.raise(2));
  ASSERT_TRUE(queue.raise(3));

  const auto value = queue.pop();
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 3);
}

TEST(ThreadSafeQueue, RejectsItemsAfterStop) {
  safe::queue_t<int> queue;

  queue.stop();

  EXPECT_FALSE(queue.raise(1));
}
