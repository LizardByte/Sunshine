/**
 * @file src/platform/clipboard_sync.h
 * @brief Clipboard formats, subscriber tracking, and X11 selection transfers shared with tests.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// lib includes
extern "C" {
#include <moonlight-common-c/src/Limelight.h>
}

// local includes
#include "src/platform/common.h"

namespace platf {
  /**
   * @brief Largest clipboard format accepted from a client or the host.
   */
  inline constexpr std::size_t clipboard_max_bytes = SS_CLIPBOARD_DATA_MAX;

  /**
   * @brief Report whether a MIME type is plain text.
   *
   * @param mime MIME type, with or without parameters.
   * @return True for text/plain and text/plain with parameters such as a charset.
   */
  inline bool clipboard_is_text(std::string_view mime) {
    return mime == "text/plain" || mime.starts_with("text/plain;");
  }

  /**
   * @brief List the MIME types a clipboard can be offered as.
   *
   * @param data Clipboard to offer.
   * @return The text MIME type when there is text, then the image MIME type when there is an image.
   */
  inline std::vector<std::string> clipboard_mime_types(const clipboard_data_t &data) {
    std::vector<std::string> types;
    if (!data.text.empty()) {
      types.emplace_back(SS_CLIPBOARD_MIME_TEXT);
    }
    if (!data.image_mime.empty()) {
      types.push_back(data.image_mime);
    }
    return types;
  }

  /**
   * @brief Find the bytes of one offered format.
   *
   * @param data Offered clipboard.
   * @param mime MIME type that was requested.
   * @return The format's bytes, or empty when the clipboard does not have that format.
   */
  inline std::optional<std::span<const std::uint8_t>> clipboard_payload(const clipboard_data_t &data, std::string_view mime) {
    if (clipboard_is_text(mime) && !data.text.empty()) {
      return std::span {reinterpret_cast<const std::uint8_t *>(data.text.data()), data.text.size()};
    }
    if (!data.image_mime.empty() && mime == data.image_mime) {
      return std::span<const std::uint8_t> {data.image};
    }
    return std::nullopt;
  }

  /**
   * @brief Choose the formats to request from a client offer.
   *
   * Sunshine takes text and one image. PNG is preferred to JPEG because it is lossless.
   *
   * @param offered MIME types in the client offer.
   * @return The offered MIME types to request.
   */
  inline std::vector<std::string> clipboard_wanted_formats(std::span<const std::string_view> offered) {
    std::vector<std::string> wanted;
    if (const auto text = std::ranges::find_if(offered, clipboard_is_text); text != offered.end()) {
      wanted.emplace_back(*text);
    }
    for (const std::string_view image : {std::string_view {SS_CLIPBOARD_MIME_PNG}, std::string_view {SS_CLIPBOARD_MIME_JPEG}}) {
      if (std::ranges::find(offered, image) != offered.end()) {
        wanted.emplace_back(image);
        break;
      }
    }
    return wanted;
  }

  /**
   * @brief Convert ISO-8859-1 text, which X11 uses for the STRING target, to UTF-8.
   *
   * @param latin1 Latin-1 bytes.
   * @return The same text in UTF-8.
   */
  inline std::string latin1_to_utf8(std::string_view latin1) {
    std::string utf8;
    utf8.reserve(latin1.size());
    for (const char c : latin1) {
      const auto byte = static_cast<unsigned char>(c);
      if (byte < 0x80) {
        utf8.push_back(c);
      } else {
        utf8.push_back(static_cast<char>(0xC0 | (byte >> 6)));
        utf8.push_back(static_cast<char>(0x80 | (byte & 0x3F)));
      }
    }
    return utf8;
  }

  /**
   * @brief One client waiting for host clipboard updates.
   */
  struct clipboard_subscriber_t {
    clipboard_queue_t queue;  ///< Queue drained by that client's control stream.
    std::shared_ptr<const clipboard_data_t> last;  ///< Clipboard this client last received or supplied. Null before the first one.
  };

  /**
   * @brief Queues that should be offered one clipboard, and the offer sequence to use.
   */
  struct clipboard_delivery_t {
    std::uint32_t sequence = 0;  ///< Sequence number of the offer.
    std::vector<clipboard_queue_t> queues;  ///< Subscribers that do not have this clipboard yet.
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
     * @return True when the queue is new and still needs the current clipboard.
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
      entries_.push_back(clipboard_subscriber_t {std::move(queue), nullptr});
      return true;
    }

    /**
     * @brief Drop a client queue when its session stops.
     *
     * @param queue Queue that should no longer receive clipboard offers.
     */
    void remove(const clipboard_queue_t &queue) {
      std::erase_if(entries_, [&queue](const auto &entry) {
        return entry.queue == queue;
      });
    }

    /**
     * @brief Record a new host clipboard and select the subscribers that need it.
     *
     * The origin client supplied this clipboard, so it is recorded without an offer.
     * A subscriber that has not received a clipboard yet always gets one.
     *
     * @param data New host clipboard.
     * @param origin Queue of the client that supplied the clipboard, or null for a host copy.
     * @return Queues to offer the clipboard to, with a new sequence number when there are any.
     */
    clipboard_delivery_t publish(const std::shared_ptr<const clipboard_data_t> &data, const clipboard_queue_t &origin = {}) {
      clipboard_delivery_t delivery;
      for (auto &entry : entries_) {
        const bool is_origin = origin && entry.queue == origin;
        if (!is_origin && entry.last && *entry.last == *data) {
          continue;
        }
        entry.last = data;
        if (!is_origin) {
          delivery.queues.push_back(entry.queue);
        }
      }
      if (!delivery.queues.empty()) {
        sequence_ = sequence_ == UINT32_MAX ? 1 : sequence_ + 1;
        delivery.sequence = sequence_;
      }
      return delivery;
    }

  private:
    std::vector<clipboard_subscriber_t> entries_;  ///< Subscribers registered for the current process.
    std::uint32_t sequence_ = 0;  ///< Sequence number of the last offer.
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
   * @brief Byte width of one X property item, as stored in selection_fragment_t::data.
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
   * A larger property is read in several slices. X11 deletes it only after the
   * slice that reaches its end.
   */
  inline constexpr unsigned long selection_property_long_length = 64 * 1024;

  /**
   * @brief One slice of a selection property, as XGetWindowProperty returns it.
   *
   * Format-32 items are stored as 4 bytes each in host byte order.
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
    bool truncated = false;  ///< Whether the property was larger than clipboard_max_bytes.
  };

  /**
   * @brief Copy property bytes without passing the protocol cap.
   *
   * @param out Destination bytes.
   * @param chunk Bytes returned by one property read.
   * @return False when some bytes did not fit under the cap.
   */
  inline bool append_property_bytes(std::vector<std::byte> &out, std::span<const std::byte> chunk) {
    const auto take = std::min(chunk.size(), clipboard_max_bytes - std::min(out.size(), clipboard_max_bytes));
    out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(take));
    return take == chunk.size();
  }

  /**
   * @brief Read a selection property until X11 can delete it.
   *
   * Each fetch uses delete=True and selection_property_long_length. XGetWindowProperty
   * deletes the property only when bytes_after is zero, so a property larger than one read
   * is fetched again at the next 32-bit offset. Stored bytes stop at the protocol cap.
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
      if (!append_property_bytes(loaded.data, fragment.data)) {
        loaded.truncated = true;
      }
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
   * @brief Selection value received directly or assembled from INCR chunks.
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
     * @brief Accept one selection property and maybe finish a value.
     *
     * An INCR property carries a byte-count announcement, not data. Format-8 chunks of
     * the expected type are appended until an empty property ends the transfer. A chunk
     * of another type, or data over clipboard_max_bytes, rejects the transfer. The transfer
     * stays active until the empty property so every chunk is still deleted.
     *
     * @param actual_type Property type atom.
     * @param actual_format Property format, in bits.
     * @param data Property bytes.
     * @param truncated Whether the property was larger than clipboard_max_bytes.
     * @param expected_type Type atom of the requested target.
     * @param incr_atom Atom for INCR.
     * @return The completed value, or empty when the transfer is unfinished or rejected.
     */
    std::optional<std::vector<std::byte>> consume(
      std::uint64_t actual_type,
      int actual_format,
      std::span<const std::byte> data,
      bool truncated,
      std::uint64_t expected_type,
      std::uint64_t incr_atom
    ) {
      const bool expected = actual_format == 8 && actual_type == expected_type;
      if (active_) {
        if (data.empty()) {
          active_ = false;
          auto done = std::exchange(data_, {});
          if (std::exchange(rejected_, false)) {
            return std::nullopt;
          }
          return done;
        }
        if (!expected || truncated || !append_property_bytes(data_, data)) {
          rejected_ = true;
          data_.clear();
        }
        return std::nullopt;
      }

      if (actual_type == incr_atom && actual_format == 32) {
        active_ = true;
        rejected_ = false;
        data_.clear();
        return std::nullopt;
      }

      if (expected && !truncated) {
        return std::vector<std::byte> {data.begin(), data.end()};
      }
      return std::nullopt;
    }

  private:
    bool active_ = false;  ///< Whether the empty INCR chunk has not arrived yet.
    bool rejected_ = false;  ///< Whether an unsupported or oversized chunk discarded this INCR transfer.
    std::vector<std::byte> data_;  ///< Bytes accumulated from INCR chunks.
  };

  /**
   * @brief Slices of a value Sunshine sends to a requestor with the INCR protocol.
   *
   * The requestor deletes the property after reading each slice. Sunshine then writes
   * the next slice, and finally an empty one.
   */
  class incr_sender_t {
  public:
    /**
     * @brief Start sending a value.
     *
     * @param size Total bytes to send.
     * @param chunk_size Largest slice written at once. Must be nonzero.
     */
    incr_sender_t(std::size_t size, std::size_t chunk_size):
        size_ {size},
        chunk_size_ {chunk_size} {
    }

    /**
     * @brief Report whether the empty terminating slice has been handed out.
     *
     * @return True when the transfer is complete.
     */
    [[nodiscard]] bool done() const {
      return done_;
    }

    /**
     * @brief Take the next slice to write.
     *
     * @return Offset and length of the next slice. A zero length is the terminator.
     */
    std::pair<std::size_t, std::size_t> next() {
      const auto length = std::min(chunk_size_, size_ - offset_);
      const std::pair slice {offset_, length};
      offset_ += length;
      done_ = length == 0;
      return slice;
    }

  private:
    std::size_t size_;  ///< Total bytes to send.
    std::size_t chunk_size_;  ///< Largest slice written at once.
    std::size_t offset_ = 0;  ///< Bytes already handed out.
    bool done_ = false;  ///< Whether the terminator was handed out.
  };
}  // namespace platf
