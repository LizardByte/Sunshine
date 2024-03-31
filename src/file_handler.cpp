/**
 * @file file_handler.cpp
 * @brief File handling functions.
 */

// standard includes
#include <filesystem>
#include <fstream>

// local includes
#include "file_handler.h"
#include "logging.h"

namespace file_handler {

  /**
   * @brief Read a file to string.
   * @param path The path of the file.
   * @return `std::string` : The contents of the file.
   *
   * EXAMPLES:
   * ```cpp
   * std::string contents = read_file("path/to/file");
   * ```
   */
  std::string
  read_file(const char *path) {
    if (!std::filesystem::exists(path)) {
      BOOST_LOG(debug) << "Missing file: " << path;
      return {};
    }

    std::ifstream in(path);
    return std::string { (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>() };
  }

  /**
   * @brief Writes a file.
   * @param path The path of the file.
   * @param contents The contents to write.
   * @return `int` : `0` on success, `-1` on failure.
   *
   * EXAMPLES:
   * ```cpp
   * int write_status = write_file("path/to/file", "file contents");
   * ```
   */
  int
  write_file(const char *path, const std::string_view &contents) {
    std::ofstream out(path);

    if (!out.is_open()) {
      return -1;
    }

    out << contents;

    return 0;
  }
}  // namespace file_handler
