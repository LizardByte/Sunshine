/**
 * @file src/platform/utf_utils.cpp
 * @brief Common UTF conversion utilities used by platform-specific code.
 */
// class header include
#include "src/platform/utf_utils.h"

// standard includes
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {
  constexpr uint32_t kAsciiMax = 0x7FU;
  constexpr uint32_t kTwoByteLeadMask = 0xE0U;
  constexpr uint32_t kTwoByteLeadValue = 0xC0U;
  constexpr uint32_t kThreeByteLeadMask = 0xF0U;
  constexpr uint32_t kThreeByteLeadValue = 0xE0U;
  constexpr uint32_t kFourByteLeadMask = 0xF8U;
  constexpr uint32_t kFourByteLeadValue = 0xF0U;
  constexpr uint32_t kTwoBytePayloadMask = 0x1FU;
  constexpr uint32_t kThreeBytePayloadMask = 0x0FU;
  constexpr uint32_t kFourBytePayloadMask = 0x07U;
  constexpr uint32_t kContinuationMask = 0xC0U;
  constexpr uint32_t kContinuationValue = 0x80U;
  constexpr uint32_t kContinuationPayloadMask = 0x3FU;
  constexpr uint32_t kTwoByteMinimum = 0x80U;
  constexpr uint32_t kThreeByteMinimum = 0x800U;
  constexpr uint32_t kFourByteMinimum = 0x10000U;
  constexpr uint32_t kSurrogateStart = 0xD800U;
  constexpr uint32_t kSurrogateEnd = 0xDFFFU;
  constexpr uint32_t kUnicodeScalarMax = 0x10FFFFU;

  constexpr uint32_t to_uint(std::byte value) {
    return std::to_integer<uint32_t>(value);
  }

  constexpr bool is_overlong_encoding(uint32_t code_point, size_t continuation_bytes) {
    return (continuation_bytes == 1 && code_point < kTwoByteMinimum) ||
           (continuation_bytes == 2 && code_point < kThreeByteMinimum) ||
           (continuation_bytes == 3 && code_point < kFourByteMinimum);
  }

  constexpr bool is_invalid_scalar_value(uint32_t code_point) {
    return (code_point >= kSurrogateStart && code_point <= kSurrogateEnd) || code_point > kUnicodeScalarMax;
  }
}  // namespace

namespace utf_utils {
  bool utf8_to_utf32(std::string_view utf8, std::u32string &output) {
    std::u32string decoded;
    decoded.reserve(utf8.size());

    const auto *bytes = reinterpret_cast<const std::byte *>(utf8.data());

    for (size_t i = 0; i < utf8.size();) {
      // The first byte tells us whether this is ASCII or the start of a 2, 3, or 4 byte UTF-8 sequence.
      const auto lead = to_uint(bytes[i]);
      uint32_t code_point = 0;
      size_t continuation_bytes = 0;

      if (lead <= kAsciiMax) {
        code_point = lead;
      } else if ((lead & kTwoByteLeadMask) == kTwoByteLeadValue) {
        code_point = lead & kTwoBytePayloadMask;
        continuation_bytes = 1;
      } else if ((lead & kThreeByteLeadMask) == kThreeByteLeadValue) {
        code_point = lead & kThreeBytePayloadMask;
        continuation_bytes = 2;
      } else if ((lead & kFourByteLeadMask) == kFourByteLeadValue) {
        code_point = lead & kFourBytePayloadMask;
        continuation_bytes = 3;
      } else {
        return false;
      }

      if (i + continuation_bytes >= utf8.size()) {
        return false;
      }

      // Every continuation byte must start with binary 10xxxxxx and contributes six payload bits.
      for (size_t j = 1; j <= continuation_bytes; ++j) {
        const auto continuation = to_uint(bytes[i + j]);
        if ((continuation & kContinuationMask) != kContinuationValue) {
          return false;
        }
        code_point = (code_point << 6U) | (continuation & kContinuationPayloadMask);
      }

      // Reject non-shortest encodings, UTF-16 surrogate code points, and values outside Unicode's range.
      if (is_overlong_encoding(code_point, continuation_bytes) || is_invalid_scalar_value(code_point)) {
        return false;
      }

      decoded.push_back(static_cast<char32_t>(code_point));
      i += continuation_bytes + 1;
    }

    output = std::move(decoded);
    return true;
  }
}  // namespace utf_utils
