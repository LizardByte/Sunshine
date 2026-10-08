/**
 * @file src/platform/clipboard_sync.h
 * @brief Clipboard subscriber tracking and X11 selection decoding shared with tests.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// local includes
#include "src/platform/common.h"

namespace platf {
  /**
   * @brief Maximum UTF-8 clipboard payload accepted by the protocol.
   */
  inline constexpr std::size_t clipboard_max_bytes = 32755;

  /**
   * @brief One client waiting for host clipboard updates.
   */
  struct clipboard_subscriber_t {
    clipboard_queue_t queue;  ///< Queue drained by that client's control stream.
    std::string last_sent;  ///< Last text delivered to this client, or applied from it.
    bool has_snapshot = false;  ///< Whether this client has received its initial clipboard value.
  };

  /**
   * @brief Active clipboard subscriptions for every streaming client.
   */
  class clipboard_subscribers_t {
  public:
    /**
     * @brief Register a client queue.
     *
     * @param queue Queue drained by the client's control stream.
     * @return True when the queue is new and still needs the current clipboard snapshot.
     */
    bool add(clipboard_queue_t queue) {
      if (!queue) {
        return false;
      }
      for (const auto &entry : entries_) {
        if (entry.queue == queue) {
          return false;
        }
      }
      entries_.push_back(clipboard_subscriber_t {std::move(queue), {}, false});
      return true;
    }

    /**
     * @brief Drop a client queue when its session stops.
     *
     * @param queue Queue that should no longer receive clipboard text.
     */
    void remove(const clipboard_queue_t &queue) {
      std::erase_if(entries_, [&queue](const auto &entry) {
        return entry.queue == queue;
      });
    }

    /**
     * @brief Deliver host text to every subscriber that does not already have it.
     *
     * A subscriber that has not received a snapshot gets the text even when an older
     * subscriber was already given the same value.
     *
     * @param text UTF-8 clipboard text.
     * @return Queues that should be raised with this text.
     */
    std::vector<clipboard_queue_t> publish(const std::string &text) {
      std::vector<clipboard_queue_t> delivered;
      for (auto &entry : entries_) {
        if (auto queue = deliver(entry, text)) {
          delivered.push_back(std::move(queue));
        }
      }
      return delivered;
    }

    /**
     * @brief Record text applied from one client and forward it to the others.
     *
     * @param origin Queue belonging to the client that supplied the text.
     * @param text UTF-8 clipboard text applied on the host.
     * @return Queues other than the origin that should be raised with this text.
     */
    std::vector<clipboard_queue_t> note_local(const clipboard_queue_t &origin, const std::string &text) {
      std::vector<clipboard_queue_t> delivered;
      for (auto &entry : entries_) {
        if (origin && entry.queue == origin) {
          entry.last_sent = text;
          entry.has_snapshot = true;
          continue;
        }
        if (auto queue = deliver(entry, text)) {
          delivered.push_back(std::move(queue));
        }
      }
      return delivered;
    }

  private:
    /**
     * @brief Send text unless this subscriber already has that snapshot.
     *
     * @param entry Subscriber being considered.
     * @param text UTF-8 clipboard text.
     * @return The subscriber queue when a delivery is required.
     */
    static clipboard_queue_t deliver(clipboard_subscriber_t &entry, std::string_view text) {
      if (entry.has_snapshot && entry.last_sent == text) {
        return {};
      }
      entry.last_sent = text;
      entry.has_snapshot = true;
      return entry.queue;
    }

    std::vector<clipboard_subscriber_t> entries_;  ///< Subscribers registered for the current process.
  };

  /**
   * @brief Choose the property atom used to answer a selection request.
   *
   * Older requestors set the property to None. The selection protocol then uses the
   * target atom, and writing to property 0 is a BadAtom.
   *
   * @param requested Property atom from the selection request. Zero means None.
   * @param target Target atom from the selection request.
   * @param supported Whether Sunshine can convert the requested target.
   * @return Property atom for the reply, or zero when the conversion is refused.
   */
  inline std::uint64_t selection_reply_property(std::uint64_t requested, std::uint64_t target, bool supported) {
    if (!supported) {
      return 0;
    }
    if (requested == 0) {
      return target;
    }
    return requested;
  }

  /**
   * @brief Byte width of one X property item.
   *
   * @param format Property format in bits. 8, 16, and 32 are the X formats.
   * @return Number of bytes in one item. Unknown formats count as one byte.
   */
  inline std::size_t property_byte_width(int format) {
    if (format == 16) {
      return 2;
    }
    if (format == 32) {
      return 4;
    }
    return 1;
  }

  /**
   * @brief 32-bit units to advance after one XGetWindowProperty read.
   *
   * The X offset is in 32-bit units. A zero step with bytes still remaining cannot
   * make progress, so the caller deletes the property instead of reading forever.
   *
   * @param format Property format in bits.
   * @param item_count Items returned by the read.
   * @return Offset advance, or zero when the read returned no complete 32-bit unit.
   */
  inline unsigned long selection_offset_step(int format, std::size_t item_count) {
    const auto nbytes = item_count * property_byte_width(format);
    return static_cast<unsigned long>(nbytes / 4);
  }

  /**
   * @brief Maximum 32-bit units read from one selection property at a time.
   *
   * This is the protocol cap rounded up to a 32-bit unit, which is the longest read
   * XGetWindowProperty is given. A larger chunk stays on the window until a later read
   * or an explicit delete brings bytes_after to zero.
   */
  inline constexpr unsigned long selection_property_long_length = (clipboard_max_bytes + 3) / 4;

  /**
   * @brief One slice of a selection property, as XGetWindowProperty returns it.
   */
  struct selection_fragment_t {
    bool ok = false;  ///< Whether the property read succeeded.
    std::uint64_t type = 0;  ///< Actual property type atom.
    int format = 0;  ///< Actual property format, in bits.
    std::size_t item_count = 0;  ///< Items stored in data.
    std::size_t bytes_after = 0;  ///< Bytes still unread. X11 deletes the property only when this is zero.
    std::vector<std::byte> data;  ///< Bytes returned by this read.
  };

  /**
   * @brief Selection property assembled up to the protocol cap.
   */
  struct loaded_selection_t {
    std::uint64_t type = 0;  ///< Property type from the first read.
    int format = 0;  ///< Property format from the first read.
    std::vector<std::byte> data;  ///< Property bytes, capped at clipboard_max_bytes.
    bool acknowledged = false;  ///< Whether the last read had no unread tail, so X11 deleted the property.
  };

  /**
   * @brief Copy property bytes without passing the protocol cap.
   *
   * @param out Destination bytes.
   * @param chunk Bytes returned by one property read.
   */
  inline void append_property_bytes(std::vector<std::byte> &out, const std::vector<std::byte> &chunk) {
    if (chunk.empty() || out.size() >= clipboard_max_bytes) {
      return;
    }
    const auto take = std::min(chunk.size(), clipboard_max_bytes - out.size());
    const auto offset = out.size();
    out.resize(offset + take);
    std::copy_n(chunk.data(), take, out.data() + offset);
  }

  /**
   * @brief Read a selection property until X11 can delete it.
   *
   * Each fetch uses delete=True and selection_property_long_length. XGetWindowProperty
   * deletes the property only when bytes_after is zero, so a chunk larger than the read
   * is fetched again at the next 32-bit offset. Stored text still stops at the protocol cap.
   * Fetch returns ok=false when the X call fails. The fragment's data must own its bytes.
   *
   * @tparam Fetch Reads one slice. Called with the 32-bit offset and the maximum 32-bit length.
   * @param fetch Property read. It must use delete=True and own the returned bytes.
   * @return The capped property, or empty when the first read fails.
   */
  template<typename Fetch>
  std::optional<loaded_selection_t> load_selection_property(Fetch &&fetch) {
    loaded_selection_t loaded;
    unsigned long offset = 0;
    bool started = false;
    while (true) {
      const selection_fragment_t fragment = fetch(offset, selection_property_long_length);
      if (!fragment.ok) {
        if (!started) {
          return std::nullopt;
        }
        return loaded;
      }
      if (!started) {
        loaded.type = fragment.type;
        loaded.format = fragment.format;
        started = true;
      }
      append_property_bytes(loaded.data, fragment.data);
      if (fragment.bytes_after == 0) {
        loaded.acknowledged = true;
        return loaded;
      }
      const auto step = selection_offset_step(fragment.format, fragment.item_count);
      if (step == 0) {
        return loaded;
      }
      offset += step;
    }
  }

  /**
   * @brief Incremental X selection transfer assembled from INCR chunks.
   */
  class incr_transfer_t {
  public:
    /**
     * @brief Report whether an INCR transfer is waiting for more chunks.
     *
     * @return True after a size announcement and before the empty chunk.
     */
    [[nodiscard]] bool active() const {
      return active_;
    }

    /**
     * @brief Accept one selection property and maybe finish a text value.
     *
     * An INCR property carries a byte-count announcement, not clipboard text.
     * Format-8 UTF8_STRING and STRING chunks are appended until the empty property ends the transfer.
     * An unsupported type discards the bytes and rejects the transfer. The transfer stays active
     * until that empty text property so every chunk can be deleted, and the terminator is not published.
     *
     * @param actual_type Property type atom.
     * @param actual_format Property format, in bits.
     * @param item_count Number of items of that format.
     * @param data Property bytes. May be null when the item count is zero.
     * @param utf8_atom Atom for UTF8_STRING.
     * @param string_atom Atom for STRING.
     * @param incr_atom Atom for INCR.
     * @return Completed clipboard text, or empty when the transfer is unfinished or ignored.
     */
    std::optional<std::string> consume(
      std::uint64_t actual_type,
      int actual_format,
      std::size_t item_count,
      const std::byte *data,
      std::uint64_t utf8_atom,
      std::uint64_t string_atom,
      std::uint64_t incr_atom
    ) {
      if (active_) {
        if (
          const bool text_chunk = actual_format == 8 && (actual_type == utf8_atom || actual_type == string_atom);
          !text_chunk
        ) {
          rejected_ = true;
          text_.clear();
          return std::nullopt;
        }
        if (item_count == 0) {
          active_ = false;
          auto done = std::move(text_);
          text_.clear();
          const bool rejected = rejected_;
          rejected_ = false;
          if (rejected) {
            return std::nullopt;
          }
          return done;
        }
        if (!rejected_) {
          append_bytes(data, item_count);
        }
        return std::nullopt;
      }

      if (actual_type == incr_atom && actual_format == 32) {
        active_ = true;
        text_.clear();
        return std::nullopt;
      }

      if ((actual_type == utf8_atom || actual_type == string_atom) && actual_format == 8) {
        std::string text;
        append_to(text, data, item_count);
        return text;
      }

      return std::nullopt;
    }

  private:
    /**
     * @brief Copy property bytes into a string without passing the protocol cap.
     *
     * @param text Destination text.
     * @param data Property bytes. May be null when the item count is zero.
     * @param size Number of bytes available at data.
     */
    static void append_to(std::string &text, const std::byte *data, std::size_t size) {
      if (data == nullptr || size == 0 || text.size() >= clipboard_max_bytes) {
        return;
      }
      const auto take = std::min(size, clipboard_max_bytes - text.size());
      const auto offset = text.size();
      text.resize(offset + take);
      std::copy_n(data, take, reinterpret_cast<std::byte *>(text.data() + offset));
    }

    /**
     * @brief Append one INCR chunk to the transfer buffer.
     *
     * @param data Chunk bytes.
     * @param size Number of bytes in the chunk.
     */
    void append_bytes(const std::byte *data, std::size_t size) {
      append_to(text_, data, size);
    }

    bool active_ = false;  ///< Whether the empty INCR chunk has not arrived yet.
    bool rejected_ = false;  ///< Whether an unsupported chunk discarded this INCR transfer.
    std::string text_;  ///< UTF-8 text accumulated from INCR chunks.
  };
}  // namespace platf
