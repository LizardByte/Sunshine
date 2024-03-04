/**
 * @file src/logging.cpp
 * @brief Logging implementation file for the Sunshine application.
 */

// standard includes
#include <iostream>

// lib includes
#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>

// local includes
#include "logging.h"

using namespace std::literals;

namespace bl = boost::log;

boost::shared_ptr<boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>> sink;

bl::sources::severity_logger<int> verbose(0);  // Dominating output
bl::sources::severity_logger<int> debug(1);  // Follow what is happening
bl::sources::severity_logger<int> info(2);  // Should be informed about
bl::sources::severity_logger<int> warning(3);  // Strange events
bl::sources::severity_logger<int> error(4);  // Recoverable errors
bl::sources::severity_logger<int> fatal(5);  // Unrecoverable errors

/**
 * @brief Flush the log.
 *
 * EXAMPLES:
 * ```cpp
 * log_flush();
 * ```
 */
void
log_flush() {
  sink->flush();
}

/**
 * @brief Print help to stdout.
 * @param name The name of the program.
 *
 * EXAMPLES:
 * ```cpp
 * print_help("sunshine");
 * ```
 */
void
print_help(const char *name) {
  std::cout
    << "Usage: "sv << name << " [options] [/path/to/configuration_file] [--cmd]"sv << std::endl
    << "    Any configurable option can be overwritten with: \"name=value\""sv << std::endl
    << std::endl
    << "    Note: The configuration will be created if it doesn't exist."sv << std::endl
    << std::endl
    << "    --help                    | print help"sv << std::endl
    << "    --creds username password | set user credentials for the Web manager"sv << std::endl
    << "    --version                 | print the version of sunshine"sv << std::endl
    << std::endl
    << "    flags"sv << std::endl
    << "        -0 | Read PIN from stdin"sv << std::endl
    << "        -1 | Do not load previously saved state and do retain any state after shutdown"sv << std::endl
    << "           | Effectively starting as if for the first time without overwriting any pairings with your devices"sv << std::endl
    << "        -2 | Force replacement of headers in video stream"sv << std::endl
    << "        -p | Enable/Disable UPnP"sv << std::endl
    << std::endl;
}
