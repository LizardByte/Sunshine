/**
 * @file src/platform/windows/utf_utils.cpp
 * @brief Minimal UTF conversion utilities for Windows tools
 */
#include "utf_utils.h"

#include "src/logging.h"

#include <string>
#include <Windows.h>

using namespace std::literals;

namespace utf_utils {
  std::wstring from_utf8(const std::string &string) {
    // No conversion needed if the string is empty
    if (string.empty()) {
      return {};
    }

    // Get the output size required to store the string
    auto output_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string.data(), string.size(), nullptr, 0);
    if (output_size == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to get UTF-16 buffer size: "sv << winerr;
      return {};
    }

    // Perform the conversion
    std::wstring output(output_size, L'\0');
    output_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string.data(), string.size(), output.data(), output.size());
    if (output_size == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to convert string to UTF-16: "sv << winerr;
      return {};
    }

    return output;
  }

  std::string to_utf8(const std::wstring &string) {
    // No conversion needed if the string is empty
    if (string.empty()) {
      return {};
    }

    // Get the output size required to store the string
    auto output_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, string.data(), string.size(), nullptr, 0, nullptr, nullptr);
    if (output_size == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to get UTF-8 buffer size: "sv << winerr;
      return {};
    }

    // Perform the conversion
    std::string output(output_size, '\0');
    output_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, string.data(), string.size(), output.data(), output.size(), nullptr, nullptr);
    if (output_size == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to convert string to UTF-8: "sv << winerr;
      return {};
    }

    return output;
  }
}  // namespace utf_utils
