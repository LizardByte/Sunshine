/**
 * @file tests/unit/test_clipboard.cpp
 * @brief Tests for clipboard subscription lifecycle and X selection decoding.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// local includes
#include "src/platform/clipboard_sync.h"
#include "src/thread_safe.h"

namespace {
  /**
   * @brief Create a mailbox queue for one clipboard subscriber.
   *
   * @param mail Mailbox that owns the queue.
   * @param id Queue identifier unique within the mailbox.
   * @return Queue that records delivered clipboard text.
   */
  platf::clipboard_queue_t make_queue(const safe::mail_t &mail, const std::string &id) {
    return mail->queue<platf::clipboard_text_t>(id);
  }

  /**
   * @brief Raise text on the queues selected by a subscriber update.
   *
   * @param queues Queues that should receive the text.
   * @param text UTF-8 clipboard text.
   */
  void deliver(const std::vector<platf::clipboard_queue_t> &queues, const std::string &text) {
    for (const auto &queue : queues) {
      queue->raise(platf::clipboard_text_t {1, text});
    }
  }

  /**
   * @brief Remove every pending clipboard value from a queue.
   *
   * @param queue Queue to drain.
   * @return Text values in delivery order.
   */
  std::vector<std::string> drain(const platf::clipboard_queue_t &queue) {
    std::vector<std::string> texts;
    while (queue->peek()) {
      auto item = queue->pop(std::chrono::milliseconds {0});
      if (!item) {
        break;
      }
      texts.push_back(item->text);
    }
    return texts;
  }

  /**
   * @brief Selection property that disappears only after its last byte is read.
   */
  struct capped_property_t {
    std::vector<std::byte> bytes;  ///< Full property contents.
    bool exists = true;  ///< Whether the property is still on the window.
    int reads = 0;  ///< How many times the property was read.

    /**
     * @brief Return one bounded slice of the property.
     *
     * @param offset Offset in 32-bit units.
     * @param length Maximum length in 32-bit units.
     * @return The slice a deleting XGetWindowProperty read would return.
     */
    platf::selection_fragment_t read(unsigned long offset, unsigned long length) {
      platf::selection_fragment_t fragment;
      fragment.ok = true;
      fragment.type = 1;
      fragment.format = 8;
      if (!exists) {
        fragment.type = 0;
        return fragment;
      }
      ++reads;
      const auto start = static_cast<std::size_t>(offset) * 4;
      if (start >= bytes.size()) {
        exists = false;
        return fragment;
      }
      const auto take = std::min(static_cast<std::size_t>(length) * 4, bytes.size() - start);
      fragment.item_count = take;
      fragment.data.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(start),
        bytes.begin() + static_cast<std::ptrdiff_t>(start + take)
      );
      fragment.bytes_after = bytes.size() - start - take;
      if (fragment.bytes_after == 0) {
        exists = false;
      }
      return fragment;
    }
  };
}  // namespace

TEST(ClipboardSubscriptionTest, DeliversOneHostChangeToEverySubscriber) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = make_queue(mail, "clipboard-a");
  auto second = make_queue(mail, "clipboard-b");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(first));
  EXPECT_TRUE(subscribers.add(second));

  deliver(subscribers.publish("host"), "host");

  EXPECT_EQ(drain(first), (std::vector<std::string> {"host"}));
  EXPECT_EQ(drain(second), (std::vector<std::string> {"host"}));
}

TEST(ClipboardSubscriptionTest, SendsTheCurrentClipboardToASubscriptionAddedLater) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = make_queue(mail, "clipboard-a");
  auto second = make_queue(mail, "clipboard-b");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(first));
  deliver(subscribers.publish("same"), "same");
  EXPECT_EQ(drain(first), (std::vector<std::string> {"same"}));

  EXPECT_TRUE(subscribers.add(second));
  deliver(subscribers.publish("same"), "same");

  EXPECT_TRUE(drain(first).empty());
  EXPECT_EQ(drain(second), (std::vector<std::string> {"same"}));
}

TEST(ClipboardSubscriptionTest, RemovesASubscriberWhenTheSessionStops) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = make_queue(mail, "clipboard-a");
  auto second = make_queue(mail, "clipboard-b");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(first));
  EXPECT_TRUE(subscribers.add(second));
  subscribers.remove(first);

  deliver(subscribers.publish("next"), "next");

  EXPECT_TRUE(drain(first).empty());
  EXPECT_EQ(drain(second), (std::vector<std::string> {"next"}));
}

TEST(ClipboardSubscriptionTest, DoesNotEchoALocalSetToItsOrigin) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto origin = make_queue(mail, "clipboard-origin");
  auto other = make_queue(mail, "clipboard-other");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(origin));
  EXPECT_TRUE(subscribers.add(other));

  deliver(subscribers.note_local(origin, "from-origin"), "from-origin");
  EXPECT_TRUE(drain(origin).empty());
  EXPECT_EQ(drain(other), (std::vector<std::string> {"from-origin"}));

  EXPECT_TRUE(subscribers.publish("from-origin").empty());
}

TEST(ClipboardSubscriptionTest, ResubscribingTheSameQueueDoesNotClearItsSnapshot) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto queue = make_queue(mail, "clipboard-a");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(queue));
  deliver(subscribers.publish("kept"), "kept");
  EXPECT_FALSE(subscribers.add(queue));

  EXPECT_TRUE(subscribers.publish("kept").empty());
  EXPECT_EQ(drain(queue), (std::vector<std::string> {"kept"}));
}

TEST(ClipboardSelectionTest, UsesTheTargetAtomWhenTheSelectionPropertyIsNone) {
  EXPECT_EQ(platf::selection_reply_property(0, 44, true), 44);
  EXPECT_EQ(platf::selection_reply_property(7, 44, true), 7);
}

TEST(ClipboardSelectionTest, RefusesAnUnsupportedSelectionTarget) {
  EXPECT_EQ(platf::selection_reply_property(0, 44, false), 0);
  EXPECT_EQ(platf::selection_reply_property(7, 44, false), 0);
}

TEST(ClipboardSelectionTest, DoesNotPublishAnIncrSizeAnnouncement) {
  constexpr std::uint64_t utf8 = 1;
  constexpr std::uint64_t string_atom = 2;
  constexpr std::uint64_t incr = 3;
  constexpr std::array<std::byte, 4> announcement {std::byte {0x2a}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00}};
  platf::incr_transfer_t transfer;

  const auto text = transfer.consume(incr, 32, 1, announcement.data(), utf8, string_atom, incr);

  EXPECT_FALSE(text);
  EXPECT_TRUE(transfer.active());
}

TEST(ClipboardSelectionTest, PublishesIncrChunksOnlyAfterTheTransferEnds) {
  constexpr std::uint64_t utf8 = 1;
  constexpr std::uint64_t string_atom = 2;
  constexpr std::uint64_t incr = 3;
  constexpr std::array<std::byte, 4> announcement {std::byte {0x04}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00}};
  constexpr std::array<std::byte, 4> chunk {std::byte {'a'}, std::byte {'b'}, std::byte {'c'}, std::byte {'d'}};
  platf::incr_transfer_t transfer;
  EXPECT_FALSE(transfer.consume(incr, 32, 1, announcement.data(), utf8, string_atom, incr));
  EXPECT_FALSE(transfer.consume(utf8, 8, chunk.size(), chunk.data(), utf8, string_atom, incr));

  const auto text = transfer.consume(utf8, 8, 0, nullptr, utf8, string_atom, incr);

  ASSERT_TRUE(text);
  EXPECT_EQ(*text, "abcd");
  EXPECT_FALSE(transfer.active());
}

TEST(ClipboardSelectionTest, IgnoresUnsupportedSelectionPropertyData) {
  constexpr std::uint64_t utf8 = 1;
  constexpr std::uint64_t string_atom = 2;
  constexpr std::uint64_t incr = 3;
  constexpr std::array<std::byte, 4> chunk {std::byte {'n'}, std::byte {'o'}, std::byte {'p'}, std::byte {'e'}};
  platf::incr_transfer_t transfer;

  const auto text = transfer.consume(99, 8, chunk.size(), chunk.data(), utf8, string_atom, incr);

  EXPECT_FALSE(text);
  EXPECT_FALSE(transfer.active());
}

TEST(ClipboardSelectionTest, CapsClipboardTextAtTheProtocolLimit) {
  constexpr std::uint64_t utf8 = 1;
  constexpr std::uint64_t string_atom = 2;
  constexpr std::uint64_t incr = 3;
  const std::vector<std::byte> oversized(platf::clipboard_max_bytes + 10, std::byte {'z'});
  platf::incr_transfer_t transfer;

  const auto text = transfer.consume(utf8, 8, oversized.size(), oversized.data(), utf8, string_atom, incr);

  ASSERT_TRUE(text);
  EXPECT_EQ(text->size(), platf::clipboard_max_bytes);
}

TEST(ClipboardSelectionTest, DropsAnUnsupportedTypeReceivedDuringAnIncrTransfer) {
  constexpr std::uint64_t utf8 = 1;
  constexpr std::uint64_t string_atom = 2;
  constexpr std::uint64_t incr = 3;
  constexpr std::array<std::byte, 4> announcement {std::byte {0x04}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00}};
  constexpr std::array<std::byte, 4> chunk {std::byte {'n'}, std::byte {'o'}, std::byte {'p'}, std::byte {'e'}};
  platf::incr_transfer_t transfer;
  EXPECT_FALSE(transfer.consume(incr, 32, 1, announcement.data(), utf8, string_atom, incr));

  EXPECT_FALSE(transfer.consume(99, 8, chunk.size(), chunk.data(), utf8, string_atom, incr));
  EXPECT_TRUE(transfer.active());

  const auto text = transfer.consume(utf8, 8, 0, nullptr, utf8, string_atom, incr);

  EXPECT_FALSE(text);
  EXPECT_FALSE(transfer.active());
}

TEST(ClipboardSelectionTest, AcceptsANewIncrTransferAfterRejectingAnUnsupportedChunk) {
  constexpr std::uint64_t utf8 = 1;
  constexpr std::uint64_t string_atom = 2;
  constexpr std::uint64_t incr = 3;
  constexpr std::array<std::byte, 4> announcement {std::byte {0x04}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00}};
  constexpr std::array<std::byte, 4> chunk {std::byte {'a'}, std::byte {'b'}, std::byte {'c'}, std::byte {'d'}};
  platf::incr_transfer_t transfer;
  EXPECT_FALSE(transfer.consume(incr, 32, 1, announcement.data(), utf8, string_atom, incr));
  EXPECT_FALSE(transfer.consume(99, 8, chunk.size(), chunk.data(), utf8, string_atom, incr));
  EXPECT_FALSE(transfer.consume(utf8, 8, 0, nullptr, utf8, string_atom, incr));

  EXPECT_FALSE(transfer.consume(incr, 32, 1, announcement.data(), utf8, string_atom, incr));
  EXPECT_FALSE(transfer.consume(utf8, 8, chunk.size(), chunk.data(), utf8, string_atom, incr));
  const auto text = transfer.consume(utf8, 8, 0, nullptr, utf8, string_atom, incr);

  ASSERT_TRUE(text);
  EXPECT_EQ(*text, "abcd");
}

TEST(ClipboardSelectionTest, AcknowledgesAChunkLargerThanTheReadLimit) {
  capped_property_t property;
  property.bytes.assign(64 * 1024, std::byte {'A'});
  property.bytes[platf::selection_property_long_length * 4] = std::byte {'B'};

  const auto loaded = platf::load_selection_property([&property](unsigned long offset, unsigned long length) {
    return property.read(offset, length);
  });

  ASSERT_TRUE(loaded);
  EXPECT_FALSE(property.exists);
  EXPECT_GE(property.reads, 2);
  EXPECT_TRUE(loaded->acknowledged);
  EXPECT_EQ(loaded->data.size(), platf::clipboard_max_bytes);
  EXPECT_EQ(loaded->data.front(), std::byte {'A'});
  EXPECT_EQ(loaded->data.back(), std::byte {'A'});
}

TEST(ClipboardSelectionTest, ReadsAShortPropertyInOneCall) {
  const std::vector property {std::byte {'h'}, std::byte {'i'}};
  int reads = 0;
  const auto loaded = platf::load_selection_property([&property, &reads](unsigned long, unsigned long) {
    ++reads;
    platf::selection_fragment_t fragment;
    fragment.ok = true;
    fragment.type = 1;
    fragment.format = 8;
    fragment.item_count = property.size();
    fragment.data = property;
    fragment.bytes_after = 0;
    return fragment;
  });

  ASSERT_TRUE(loaded);
  EXPECT_EQ(reads, 1);
  EXPECT_TRUE(loaded->acknowledged);
  EXPECT_EQ(loaded->data, property);
}

TEST(ClipboardSelectionTest, LeavesAnUnreadableTailForTheCallerToDelete) {
  int reads = 0;
  const auto loaded = platf::load_selection_property([&reads](unsigned long, unsigned long) {
    ++reads;
    platf::selection_fragment_t fragment;
    fragment.ok = true;
    fragment.type = 1;
    fragment.format = 8;
    fragment.item_count = 1;
    fragment.bytes_after = 10;
    fragment.data = {std::byte {'x'}};
    return fragment;
  });

  ASSERT_TRUE(loaded);
  EXPECT_EQ(reads, 1);
  EXPECT_FALSE(loaded->acknowledged);
  EXPECT_EQ(loaded->data, (std::vector<std::byte> {std::byte {'x'}}));
}

TEST(ClipboardSelectionTest, IgnoresAFailedPropertyRead) {
  const auto loaded = platf::load_selection_property([](unsigned long, unsigned long) {
    return platf::selection_fragment_t {};
  });

  EXPECT_FALSE(loaded);
}
