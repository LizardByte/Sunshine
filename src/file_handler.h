/**
 * @file file_handler.h
 * @brief Declarations for file handling functions.
 */
#pragma once

// standard includes
#include <string>

/**
 * @brief Responsible for file handling functions.
 */
namespace file_handler {
  /**
   * @brief Get the parent directory of a file or directory.
   * @param path The path of the file or directory.
   * @return The parent directory.
   * @examples
   * std::string parent_dir = get_parent_directory("path/to/file");
   * @examples_end
   */
  std::string get_parent_directory(const std::string &path);

  /**
   * @brief Make a directory.
   * @param path The path of the directory.
   * @return `true` on success, `false` on failure.
   * @examples
   * bool dir_created = make_directory("path/to/directory");
   * @examples_end
   */
  bool make_directory(const std::string &path);

  /**
   * @brief Read a file to string.
   * @param path The path of the file.
   * @return The contents of the file.
   * @examples
   * std::string contents = read_file("path/to/file");
   * @examples_end
   */
  std::string read_file(const char *path);

  /**
   * @brief Writes a file.
   * @param path The path of the file.
   * @param contents The contents to write.
   * @return ``0`` on success, ``-1`` on failure.
   * @examples
   * int write_status = write_file("path/to/file", "file contents");
   * @examples_end
   */
  int write_file(const char *path, const std::string_view &contents);
}  // namespace file_handler
