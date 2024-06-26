/**
 * @file src/logging.h
 * @brief Declarations for logging related functions.
 */
#pragma once

// lib includes
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>

using text_sink = boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>;

extern boost::log::sources::severity_logger<int> verbose;
extern boost::log::sources::severity_logger<int> debug;
extern boost::log::sources::severity_logger<int> info;
extern boost::log::sources::severity_logger<int> warning;
extern boost::log::sources::severity_logger<int> error;
extern boost::log::sources::severity_logger<int> fatal;

/**
 * @brief Handles the initialization and deinitialization of the logging system.
 */
namespace logging {
  class deinit_t {
  public:
    /**
     * @brief A destructor that restores the initial state.
     */
    ~deinit_t();
  };

  /**
   * @brief Deinitialize the logging system.
   * @examples
   * deinit();
   * @examples_end
   */
  void
  deinit();

  /**
   * @brief Initialize the logging system.
   * @param min_log_level The minimum log level to output.
   * @param log_file The log file to write to.
   * @return An object that will deinitialize the logging system when it goes out of scope.
   * @examples
   * log_init(2, "sunshine.log");
   * @examples_end
   */
  [[nodiscard]] std::unique_ptr<deinit_t>
  init(int min_log_level, const std::string &log_file);

  /**
   * @brief Setup AV logging.
   * @param min_log_level The log level.
   */
  void
  setup_av_logging(int min_log_level);

  /**
   * @brief Flush the log.
   * @examples
   * log_flush();
   * @examples_end
   */
  void
  log_flush();

  /**
   * @brief Print help to stdout.
   * @param name The name of the program.
   * @examples
   * print_help("sunshine");
   * @examples_end
   */
  void
  print_help(const char *name);
}  // namespace logging
