/**
 * @file tests/unit/test_thread_safe.cpp
 * @brief Tests for thread-safe utility types.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <cstdint>
#include <type_traits>

// local includes
#include "src/thread_safe.h"

static_assert(!std::is_convertible_v<std::uint32_t, safe::queue_t<int>>);

/**
 * @brief Const event inspection observes state transitions without consuming the pending value.
 */
TEST(ThreadSafeEvent, ConstPeekPreservesPendingValuesAcrossStateChanges) {
  safe::event_t<int> event;
  const auto &view = event;
  EXPECT_FALSE(view.peek());
  event.raise(42);
  EXPECT_TRUE(view.peek());
  EXPECT_TRUE(view.peek());
  EXPECT_EQ(event.try_pop(), 42);
  EXPECT_FALSE(view.peek());
  event.raise(7);
  event.stop();
  EXPECT_FALSE(view.peek());
  event.reset();
  EXPECT_FALSE(view.peek());
  event.raise(9);
  EXPECT_TRUE(view.peek());
}

/**
 * @brief Mail cleanup removes an expired post while preserving an active post's owner.
 */
TEST(ThreadSafeMail, CleanupPreservesLivePostsAndRemovesExpiredPosts) {
  safe::mail_raw_t mailbox;
  const auto live = std::make_shared<int>(7);
  mailbox.id_to_post.try_emplace("live", live);
  mailbox.id_to_post.try_emplace("expired", std::weak_ptr<void> {});

  mailbox.cleanup();

  EXPECT_EQ(mailbox.id_to_post.size(), 1);
  EXPECT_EQ(mailbox.id_to_post.at("live").lock(), live);
  mailbox.cleanup();
  EXPECT_EQ(mailbox.id_to_post.size(), 1);
}

/**
 * @brief Repeated lookups reuse each live mailbox channel and preserve queued values.
 */
TEST(ThreadSafeMail, ReusesExistingEventAndQueueForDuplicateIdentifiers) {
  const auto mailbox = std::make_shared<safe::mail_raw_t>();
  const auto event = mailbox->event<int>("event");
  event->raise(0);
  const auto same_event = mailbox->event<int>("event");
  EXPECT_EQ(same_event, event);
  EXPECT_EQ(same_event->pop(), 0);

  const auto queue = mailbox->queue<int>("queue");
  ASSERT_TRUE(queue->raise(7));
  const auto same_queue = mailbox->queue<int>("queue");
  EXPECT_EQ(same_queue, queue);
  EXPECT_EQ(same_queue->pop(), 7);
  EXPECT_EQ(mailbox->id_to_post.size(), 2);
}

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
