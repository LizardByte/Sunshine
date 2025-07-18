/**
 * @file src/platform/windows/tools/helper.h
 * @brief Helpers for tools.
 */
#pragma once

// standard includes
#include <iostream>
#include <string>

// lib includes
#include <boost/locale.hpp>

// platform includes
#include <Windows.h>

/**
 * @brief Safe console output utilities for Windows
 * These functions prevent crashes when outputting strings with special characters.
 * This is only used in tools/audio-info and tools/dxgi-info.
 */
namespace output {
  // ASCII character range constants for safe output, https://www.ascii-code.com/
  static constexpr int ASCII_PRINTABLE_START = 32;
  static constexpr int ASCII_PRINTABLE_END = 127;

  /**
   * @brief Return a non-null wide string, defaulting to "Unknown" if null
   * @param str The wide string to check
   * @return A non-null wide string
   */
  inline const wchar_t *no_null(const wchar_t *str) {
    return str ? str : L"Unknown";
  }

  /**
   * @brief Safely convert a wide string to console output using Windows API
   * @param wstr The wide string to output
   */
  inline void safe_wcout(const std::wstring &wstr) {
    if (wstr.empty()) {
      return;
    }

    // Try to use the Windows console API for proper Unicode output
    if (const HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); hConsole != INVALID_HANDLE_VALUE) {
      DWORD written;
      if (WriteConsoleW(hConsole, wstr.c_str(), wstr.length(), &written, nullptr)) {
        return;  // Success with WriteConsoleW
      }
    }

    // Fallback: convert to narrow string and output to std::cout
    try {
      const std::string narrow_str = boost::locale::conv::utf_to_utf<char>(wstr);
      std::cout << narrow_str;
    } catch (const boost::locale::conv::conversion_error &) {
      // Final fallback: output character by character, replacing non-ASCII
      for (const wchar_t wc : wstr) {
        if (wc >= ASCII_PRINTABLE_START && wc < ASCII_PRINTABLE_END) {  // Printable ASCII
          std::cout << static_cast<char>(wc);
        } else {
          std::cout << '?';
        }
      }
    }
  }

  /**
   * @brief Safely convert a wide string literal to console encoding and output it
   * @param wstr The wide string literal to output
   */
  inline void safe_wcout(const wchar_t *wstr) {
    if (wstr) {
      safe_wcout(std::wstring(wstr));
    } else {
      std::cout << "Unknown";
    }
  }

  /**
   * @brief Safely convert a string to wide string and then to console output
   * @param str The string to output
   */
  inline void safe_cout(const std::string &str) {
    if (str.empty()) {
      return;
    }

    try {
      // Convert string to wide string first, then to console output
      const std::wstring wstr = boost::locale::conv::utf_to_utf<wchar_t>(str);
      safe_wcout(wstr);
    } catch (const boost::locale::conv::conversion_error &) {
      // Fallback: output string directly, replacing problematic characters
      for (const char c : str) {
        if (c >= ASCII_PRINTABLE_START && c < ASCII_PRINTABLE_END) {  // Printable ASCII
          std::cout << c;
        } else {
          std::cout << '?';
        }
      }
    }
  }

  /**
   * @brief Output a label and value pair safely
   * @param label The label to output
   * @param value The wide string value to output
   */
  inline void output_field(const std::string &label, const wchar_t *value) {
    std::cout << label << " : ";
    safe_wcout(value ? value : L"Unknown");
    std::cout << std::endl;
  }

  /**
   * @brief Output a label and string value pair
   * @param label The label to output
   * @param value The string value to output
   */
  inline void output_field(const std::string &label, const std::string &value) {
    std::cout << label << " : ";
    safe_cout(value);
    std::cout << std::endl;
  }
}  // namespace output
