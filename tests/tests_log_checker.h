/**
 * @file tests/tests_log_checker.h
 * @brief Utility functions to check log file contents.
 */
#pragma once

#include <algorithm>
#include <fstream>
#include <regex>
#include <src/logging.h>
#include <string>

namespace log_checker {

  /**
   * @brief Remove the timestamp prefix from a log line.
   * @param line The log line.
   * @return The log line without the timestamp prefix.
   */
  inline std::string remove_timestamp_prefix(const std::string &line) {
    static const std::regex timestamp_regex(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\]: )");
    return std::regex_replace(line, timestamp_regex, "");
  }

  /**
   * @brief Check if a log file contains a line that starts with the given string.
   * @param log_file Path to the log file.
   * @param start_str The string that the line should start with.
   * @return True if such a line is found, false otherwise.
   */
  inline bool line_starts_with(const std::string &log_file, const std::string_view &start_str) {
    logging::log_flush();

    std::ifstream input(log_file);
    if (!input.is_open()) {
      return false;
    }

    for (std::string line; std::getline(input, line);) {
      line = remove_timestamp_prefix(line);
      if (line.rfind(start_str, 0) == 0) {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Check if a log file contains a line that ends with the given string.
   * @param log_file Path to the log file.
   * @param end_str The string that the line should end with.
   * @return True if such a line is found, false otherwise.
   */
  inline bool line_ends_with(const std::string &log_file, const std::string_view &end_str) {
    logging::log_flush();

    std::ifstream input(log_file);
    if (!input.is_open()) {
      return false;
    }

    for (std::string line; std::getline(input, line);) {
      line = remove_timestamp_prefix(line);
      if (line.size() >= end_str.size() &&
          line.compare(line.size() - end_str.size(), end_str.size(), end_str) == 0) {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Check if a log file contains a line that equals the given string.
   * @param log_file Path to the log file.
   * @param str The string that the line should equal.
   * @return True if such a line is found, false otherwise.
   */
  inline bool line_equals(const std::string &log_file, const std::string_view &str) {
    logging::log_flush();

    std::ifstream input(log_file);
    if (!input.is_open()) {
      return false;
    }

    for (std::string line; std::getline(input, line);) {
      line = remove_timestamp_prefix(line);
      if (line == str) {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Check if a log file contains a line that contains the given substring.
   * @param log_file Path to the log file.
   * @param substr The substring to search for.
   * @param case_insensitive Whether the search should be case-insensitive.
   * @return True if such a line is found, false otherwise.
   */
  inline bool line_contains(const std::string &log_file, const std::string_view &substr, bool case_insensitive = false) {
    logging::log_flush();

    std::ifstream input(log_file);
    if (!input.is_open()) {
      return false;
    }

    std::string search_str(substr);
    if (case_insensitive) {
      // sonarcloud complains about this, but the solution doesn't work for macOS-12
      std::transform(search_str.begin(), search_str.end(), search_str.begin(), ::tolower);
    }

    for (std::string line; std::getline(input, line);) {
      line = remove_timestamp_prefix(line);
      if (case_insensitive) {
        // sonarcloud complains about this, but the solution doesn't work for macOS-12
        std::transform(line.begin(), line.end(), line.begin(), ::tolower);
      }
      if (line.find(search_str) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

}  // namespace log_checker
