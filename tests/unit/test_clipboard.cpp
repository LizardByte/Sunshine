/**
 * @file tests/unit/test_clipboard.cpp
 * @brief Tests for clipboard formats, subscription lifecycle, and X selection transfers.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
   * @return Queue that records clipboard offers.
   */
  platf::clipboard_queue_t make_queue(const safe::mail_t &mail, const std::string &id) {
    return mail->queue<platf::clipboard_message_t>(id);
  }

  /**
   * @brief Make a shared clipboard holding text.
   *
   * @param text UTF-8 clipboard text.
   * @return Clipboard with only text.
   */
  std::shared_ptr<const platf::clipboard_data_t> text_clipboard(const std::string &text) {
    return std::make_shared<const platf::clipboard_data_t>(platf::clipboard_data_t {text, {}, {}});
  }

  /**
   * @brief Raise an offer on the queues selected by a subscriber update.
   *
   * @param delivery Queues and sequence selected by the update.
   * @param data Clipboard to offer.
   */
  void deliver(const platf::clipboard_delivery_t &delivery, const std::shared_ptr<const platf::clipboard_data_t> &data) {
    for (const auto &queue : delivery.queues) {
      queue->raise(platf::clipboard_message_t {platf::clipboard_action_e::offer, delivery.sequence, {}, data});
    }
  }

  /**
   * @brief Remove every pending clipboard offer from a queue.
   *
   * @param queue Queue to drain.
   * @return Offered text values in delivery order.
   */
  std::vector<std::string> drain(const platf::clipboard_queue_t &queue) {
    std::vector<std::string> texts;
    while (queue->peek()) {
      auto item = queue->pop(std::chrono::milliseconds {0});
      if (!item) {
        break;
      }
      texts.push_back(item->data->text);
    }
    return texts;
  }

  /**
   * @brief View bytes as a span.
   *
   * @param text Bytes to view.
   * @return The bytes of text.
   */
  std::span<const std::byte> bytes_of(std::string_view text) {
    return std::as_bytes(std::span {text.data(), text.size()});
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

  constexpr std::uint64_t utf8_atom = 1;  ///< Stand-in UTF8_STRING atom.
  constexpr std::uint64_t incr_atom = 3;  ///< Stand-in INCR atom.
  constexpr std::array<std::byte, 4> incr_announcement {std::byte {0x04}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00}};  ///< INCR size announcement.
}  // namespace

TEST(ClipboardSubscriptionTest, DeliversOneHostChangeToEverySubscriber) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = make_queue(mail, "clipboard-a");
  auto second = make_queue(mail, "clipboard-b");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(first));
  EXPECT_TRUE(subscribers.add(second));

  const auto host = text_clipboard("host");
  const auto delivery = subscribers.publish(host);
  deliver(delivery, host);

  EXPECT_NE(delivery.sequence, 0);
  EXPECT_EQ(drain(first), (std::vector<std::string> {"host"}));
  EXPECT_EQ(drain(second), (std::vector<std::string> {"host"}));
}

TEST(ClipboardSubscriptionTest, SendsTheCurrentClipboardToASubscriptionAddedLater) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = make_queue(mail, "clipboard-a");
  auto second = make_queue(mail, "clipboard-b");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(first));
  const auto same = text_clipboard("same");
  deliver(subscribers.publish(same), same);
  EXPECT_EQ(drain(first), (std::vector<std::string> {"same"}));

  EXPECT_TRUE(subscribers.add(second));
  deliver(subscribers.publish(text_clipboard("same")), same);

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

  const auto next = text_clipboard("next");
  deliver(subscribers.publish(next), next);

  EXPECT_TRUE(drain(first).empty());
  EXPECT_EQ(drain(second), (std::vector<std::string> {"next"}));
}

TEST(ClipboardSubscriptionTest, DoesNotOfferAClientClipboardBackToItsOrigin) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto origin = make_queue(mail, "clipboard-origin");
  auto other = make_queue(mail, "clipboard-other");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(origin));
  EXPECT_TRUE(subscribers.add(other));

  const auto from_origin = text_clipboard("from-origin");
  const auto delivery = subscribers.publish(from_origin, origin);
  deliver(delivery, from_origin);
  EXPECT_TRUE(drain(origin).empty());
  EXPECT_EQ(drain(other), (std::vector<std::string> {"from-origin"}));

  // The host selection now holds the same clipboard, so reading it back offers nothing
  EXPECT_TRUE(subscribers.publish(text_clipboard("from-origin")).queues.empty());
}

TEST(ClipboardSubscriptionTest, ResubscribingTheSameQueueDoesNotClearItsSnapshot) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto queue = make_queue(mail, "clipboard-a");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(queue));
  const auto kept = text_clipboard("kept");
  deliver(subscribers.publish(kept), kept);
  EXPECT_FALSE(subscribers.add(queue));

  EXPECT_TRUE(subscribers.publish(text_clipboard("kept")).queues.empty());
  EXPECT_EQ(drain(queue), (std::vector<std::string> {"kept"}));
}

TEST(ClipboardSubscriptionTest, UsesANewSequenceOnlyForAnOfferThatIsSent) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto queue = make_queue(mail, "clipboard-a");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(queue));

  const auto first = subscribers.publish(text_clipboard("one"));
  const auto repeat = subscribers.publish(text_clipboard("one"));
  const auto second = subscribers.publish(text_clipboard("two"));

  EXPECT_TRUE(repeat.queues.empty());
  EXPECT_EQ(repeat.sequence, 0);
  EXPECT_EQ(second.sequence, first.sequence + 1);
}

TEST(ClipboardSubscriptionTest, OffersANewImageWithUnchangedText) {
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto queue = make_queue(mail, "clipboard-a");
  platf::clipboard_subscribers_t subscribers;
  EXPECT_TRUE(subscribers.add(queue));
  EXPECT_EQ(subscribers.publish(std::make_shared<const platf::clipboard_data_t>(platf::clipboard_data_t {"caption", SS_CLIPBOARD_MIME_PNG, {1, 2}})).queues.size(), 1);

  const auto changed = subscribers.publish(std::make_shared<const platf::clipboard_data_t>(platf::clipboard_data_t {"caption", SS_CLIPBOARD_MIME_PNG, {3}}));

  EXPECT_EQ(changed.queues.size(), 1);
}

TEST(ClipboardFormatTest, OffersTextThenTheImage) {
  const platf::clipboard_data_t both {"text", SS_CLIPBOARD_MIME_JPEG, {1}};
  const platf::clipboard_data_t image_only {{}, SS_CLIPBOARD_MIME_PNG, {1}};

  EXPECT_EQ(platf::clipboard_mime_types(both), (std::vector<std::string> {SS_CLIPBOARD_MIME_TEXT, SS_CLIPBOARD_MIME_JPEG}));
  EXPECT_EQ(platf::clipboard_mime_types(image_only), (std::vector<std::string> {SS_CLIPBOARD_MIME_PNG}));
  EXPECT_TRUE(platf::clipboard_mime_types({}).empty());
}

TEST(ClipboardFormatTest, FindsTheBytesOfARequestedFormat) {
  const platf::clipboard_data_t data {"hi", SS_CLIPBOARD_MIME_PNG, {7, 8, 9}};

  const auto text = platf::clipboard_payload(data, "text/plain");
  const auto image = platf::clipboard_payload(data, SS_CLIPBOARD_MIME_PNG);

  ASSERT_TRUE(text);
  EXPECT_EQ(std::string(text->begin(), text->end()), "hi");
  ASSERT_TRUE(image);
  EXPECT_EQ(std::vector<std::uint8_t>(image->begin(), image->end()), (std::vector<std::uint8_t> {7, 8, 9}));
  EXPECT_FALSE(platf::clipboard_payload(data, SS_CLIPBOARD_MIME_JPEG));
  EXPECT_FALSE(platf::clipboard_payload({}, SS_CLIPBOARD_MIME_TEXT));
}

TEST(ClipboardFormatTest, RecognizesPlainTextWithAndWithoutParameters) {
  EXPECT_TRUE(platf::clipboard_is_text("text/plain"));
  EXPECT_TRUE(platf::clipboard_is_text(SS_CLIPBOARD_MIME_TEXT));
  EXPECT_FALSE(platf::clipboard_is_text("text/plainish"));
  EXPECT_FALSE(platf::clipboard_is_text("text/html"));
}

TEST(ClipboardFormatTest, RequestsTextAndThePreferredImage) {
  const std::array<std::string_view, 4> offered {"text/html", SS_CLIPBOARD_MIME_JPEG, "text/plain", SS_CLIPBOARD_MIME_PNG};

  EXPECT_EQ(platf::clipboard_wanted_formats(offered), (std::vector<std::string> {"text/plain", SS_CLIPBOARD_MIME_PNG}));
}

TEST(ClipboardFormatTest, RequestsJpegWhenItIsTheOnlyImage) {
  const std::array<std::string_view, 1> offered {SS_CLIPBOARD_MIME_JPEG};

  EXPECT_EQ(platf::clipboard_wanted_formats(offered), (std::vector<std::string> {SS_CLIPBOARD_MIME_JPEG}));
}

TEST(ClipboardFormatTest, RequestsNothingFromAnOfferWithoutSupportedFormats) {
  const std::array<std::string_view, 2> offered {"text/html", "image/gif"};

  EXPECT_TRUE(platf::clipboard_wanted_formats(offered).empty());
}

TEST(ClipboardFormatTest, ConvertsLatin1TextToUtf8) {
  EXPECT_EQ(platf::latin1_to_utf8("plain"), "plain");
  EXPECT_EQ(platf::latin1_to_utf8("caf\xe9 \xff"), "caf\xc3\xa9 \xc3\xbf");
}

TEST(ClipboardSelectionTest, UsesTheTargetAtomWhenTheSelectionPropertyIsNone) {
  EXPECT_EQ(platf::selection_reply_property(0, 44, true), 44);
  EXPECT_EQ(platf::selection_reply_property(7, 44, true), 7);
}

TEST(ClipboardSelectionTest, RefusesAnUnsupportedSelectionTarget) {
  EXPECT_EQ(platf::selection_reply_property(0, 44, false), 0);
  EXPECT_EQ(platf::selection_reply_property(7, 44, false), 0);
}

TEST(ClipboardSelectionTest, ReturnsAValueReceivedInOneProperty) {
  platf::incr_transfer_t transfer;

  const auto value = transfer.consume(utf8_atom, 8, bytes_of("hello"), false, utf8_atom, incr_atom);

  ASSERT_TRUE(value);
  EXPECT_EQ(*value, (std::vector<std::byte>(bytes_of("hello").begin(), bytes_of("hello").end())));
}

TEST(ClipboardSelectionTest, DoesNotPublishAnIncrSizeAnnouncement) {
  platf::incr_transfer_t transfer;

  const auto value = transfer.consume(incr_atom, 32, incr_announcement, false, utf8_atom, incr_atom);

  EXPECT_FALSE(value);
  EXPECT_TRUE(transfer.active());
}

TEST(ClipboardSelectionTest, PublishesIncrChunksOnlyAfterTheTransferEnds) {
  platf::incr_transfer_t transfer;
  EXPECT_FALSE(transfer.consume(incr_atom, 32, incr_announcement, false, utf8_atom, incr_atom));
  EXPECT_FALSE(transfer.consume(utf8_atom, 8, bytes_of("ab"), false, utf8_atom, incr_atom));
  EXPECT_FALSE(transfer.consume(utf8_atom, 8, bytes_of("cd"), false, utf8_atom, incr_atom));

  const auto value = transfer.consume(utf8_atom, 8, {}, false, utf8_atom, incr_atom);

  ASSERT_TRUE(value);
  EXPECT_EQ(*value, (std::vector<std::byte>(bytes_of("abcd").begin(), bytes_of("abcd").end())));
  EXPECT_FALSE(transfer.active());
}

TEST(ClipboardSelectionTest, IgnoresAPropertyOfAnotherType) {
  platf::incr_transfer_t transfer;

  const auto value = transfer.consume(99, 8, bytes_of("nope"), false, utf8_atom, incr_atom);

  EXPECT_FALSE(value);
  EXPECT_FALSE(transfer.active());
}

TEST(ClipboardSelectionTest, IgnoresAPropertyOverTheProtocolCap) {
  platf::incr_transfer_t transfer;

  const auto value = transfer.consume(utf8_atom, 8, bytes_of("cut"), true, utf8_atom, incr_atom);

  EXPECT_FALSE(value);
}

TEST(ClipboardSelectionTest, DropsAnIncrTransferWithAChunkOfAnotherType) {
  platf::incr_transfer_t transfer;
  EXPECT_FALSE(transfer.consume(incr_atom, 32, incr_announcement, false, utf8_atom, incr_atom));

  EXPECT_FALSE(transfer.consume(99, 8, bytes_of("nope"), false, utf8_atom, incr_atom));
  EXPECT_TRUE(transfer.active());

  const auto value = transfer.consume(99, 8, {}, false, utf8_atom, incr_atom);

  EXPECT_FALSE(value);
  EXPECT_FALSE(transfer.active());
}

TEST(ClipboardSelectionTest, DropsAnIncrTransferWithATruncatedChunk) {
  platf::incr_transfer_t transfer;
  EXPECT_FALSE(transfer.consume(incr_atom, 32, incr_announcement, false, utf8_atom, incr_atom));
  EXPECT_FALSE(transfer.consume(utf8_atom, 8, bytes_of("big"), true, utf8_atom, incr_atom));

  EXPECT_FALSE(transfer.consume(utf8_atom, 8, {}, false, utf8_atom, incr_atom));
  EXPECT_FALSE(transfer.active());
}

TEST(ClipboardSelectionTest, AcceptsANewIncrTransferAfterRejectingOne) {
  platf::incr_transfer_t transfer;
  EXPECT_FALSE(transfer.consume(incr_atom, 32, incr_announcement, false, utf8_atom, incr_atom));
  EXPECT_FALSE(transfer.consume(99, 8, bytes_of("nope"), false, utf8_atom, incr_atom));
  EXPECT_FALSE(transfer.consume(utf8_atom, 8, {}, false, utf8_atom, incr_atom));

  EXPECT_FALSE(transfer.consume(incr_atom, 32, incr_announcement, false, utf8_atom, incr_atom));
  EXPECT_FALSE(transfer.consume(utf8_atom, 8, bytes_of("abcd"), false, utf8_atom, incr_atom));
  const auto value = transfer.consume(utf8_atom, 8, {}, false, utf8_atom, incr_atom);

  ASSERT_TRUE(value);
  EXPECT_EQ(value->size(), 4);
}

TEST(ClipboardSelectionTest, StopsAppendingAtTheProtocolCap) {
  std::vector<std::byte> out(platf::clipboard_max_bytes - 2);

  EXPECT_FALSE(platf::append_property_bytes(out, bytes_of("abc")));
  EXPECT_EQ(out.size(), platf::clipboard_max_bytes);
  EXPECT_FALSE(platf::append_property_bytes(out, bytes_of("d")));
  EXPECT_EQ(out.size(), platf::clipboard_max_bytes);
}

TEST(ClipboardSelectionTest, AcknowledgesAPropertyLargerThanOneRead) {
  capped_property_t property;
  property.bytes.assign(platf::selection_property_long_length * 4 + 8, std::byte {'A'});
  property.bytes.back() = std::byte {'B'};

  const auto loaded = platf::load_selection_property([&property](unsigned long offset, unsigned long length) {
    return property.read(offset, length);
  });

  ASSERT_TRUE(loaded);
  EXPECT_FALSE(property.exists);
  EXPECT_EQ(property.reads, 2);
  EXPECT_TRUE(loaded->acknowledged);
  EXPECT_FALSE(loaded->truncated);
  EXPECT_EQ(loaded->data, property.bytes);
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

TEST(ClipboardSelectionTest, SendsIncrSlicesThenAnEmptyTerminator) {
  platf::incr_sender_t sender {10, 4};

  EXPECT_EQ(sender.next(), (std::pair<std::size_t, std::size_t> {0, 4}));
  EXPECT_EQ(sender.next(), (std::pair<std::size_t, std::size_t> {4, 4}));
  EXPECT_EQ(sender.next(), (std::pair<std::size_t, std::size_t> {8, 2}));
  EXPECT_FALSE(sender.done());
  EXPECT_EQ(sender.next(), (std::pair<std::size_t, std::size_t> {10, 0}));
  EXPECT_TRUE(sender.done());
}
