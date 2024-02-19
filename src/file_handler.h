/**
 * @file file_handler.h
 * @brief Header file for file handling functions.
 */
#pragma once

#include <string>

namespace file_handler {
  std::string
  read_file(const char *path);
  int
  write_file(const char *path, const std::string_view &contents);
}  // namespace file_handler
