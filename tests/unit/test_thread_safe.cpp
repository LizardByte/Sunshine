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

TEST(ThreadSafeShared, InitializesIndependentAutomaticOwnersAndRecreatesReleasedValues) {
  int first_constructions = 0;
  int second_constructions = 0;
  int destructions = 0;
  auto first = safe::make_shared<int>([&](int &value) {
    value = ++first_constructions;
    return 0;
  },
                                      [&](int &) {
                                        ++destructions;
                                      });
  auto second = safe::make_shared<int>([&](int &value) {
    value = 100 + ++second_constructions;
    return 0;
  },
                                       [&](int &) {
                                         ++destructions;
                                       });

  {
    auto first_value = first.ref();
    auto second_value = second.ref();
    ASSERT_TRUE(first_value);
    ASSERT_TRUE(second_value);
    EXPECT_EQ(*first_value.get(), 1);
    EXPECT_EQ(*second_value.get(), 101);
    auto same_first_value = first.ref();
    EXPECT_EQ(same_first_value.get(), first_value.get());
    EXPECT_EQ(first_constructions, 1);
  }
  EXPECT_EQ(destructions, 2);
  {
    auto recreated = first.ref();
    ASSERT_TRUE(recreated);
    EXPECT_EQ(*recreated.get(), 2);
  }
  EXPECT_EQ(destructions, 3);
}
