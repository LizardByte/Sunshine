/**
 * @file src/platform/utf_utils.h
 * @brief Common UTF conversion declarations used by platform-specific code.
 */
#pragma once

// standard includes
#include <string>
#include <string_view>

namespace utf_utils {
#ifdef _WIN32
  /**
   * @brief Convert a UTF-8 string into a UTF-16 wide string.
   * @param string The UTF-8 string.
   * @return The converted UTF-16 wide string.
   */
  std::wstring from_utf8(const std::string &string);

  /**
   * @brief Convert a UTF-16 wide string into a UTF-8 string.
   * @param string The UTF-16 wide string.
   * @return The converted UTF-8 string.
   */
  std::string to_utf8(const std::wstring &string);
#endif

  /**
   * @brief Decode UTF-8 text into UTF-32 code points.
   *
   * The conversion uses Boost.Locale to validate the UTF-8 input first, then
   * normalizes the intermediate wide string into UTF-32 so Linux input code can
   * work with fixed-width code points.
   *
   * @param utf8 The UTF-8 encoded input text.
   * @param output Receives the decoded UTF-32 code points on success.
   * @return `true` if the input is valid UTF-8, otherwise `false`.
   */
  bool utf8_to_utf32(std::string_view utf8, std::u32string &output);
}  // namespace utf_utils
