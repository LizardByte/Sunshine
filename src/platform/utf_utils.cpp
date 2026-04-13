/**
 * @file src/platform/utf_utils.cpp
 * @brief Common UTF conversion utilities used by platform-specific code.
 */
// class header include
#include "src/platform/utf_utils.h"

// lib includes
#include <boost/locale/encoding.hpp>
#include <boost/locale/encoding_errors.hpp>
#include <boost/locale/encoding_utf.hpp>

namespace utf_utils {
  bool utf8_to_utf32(std::string_view utf8, std::u32string &output) {
    if (utf8.empty()) {
      output.clear();
      return true;
    }

    try {
      // Boost.Locale defaults to skipping malformed bytes, so request the strict policy here to
      // preserve the previous behavior of rejecting invalid UTF-8 input.
      const auto wide = boost::locale::conv::to_utf<wchar_t>(utf8.data(), utf8.data() + utf8.size(), "UTF-8", boost::locale::conv::stop);

      // wchar_t is UTF-16 on Windows and UTF-32 on Linux, so normalize it to UTF-32 before
      // handing the text to the virtual keyboard path.
      output = boost::locale::conv::utf_to_utf<char32_t>(wide.data(), wide.data() + wide.size(), boost::locale::conv::stop);
      return true;
    } catch (const boost::locale::conv::conversion_error &) {
      return false;
    } catch (const boost::locale::conv::invalid_charset_error &) {
      return false;
    }
  }
}  // namespace utf_utils
