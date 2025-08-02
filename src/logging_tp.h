/**
 * @file src/logging_tp.h
 * @brief Third-party library logging setup.
 */
#pragma once

namespace logging {
  /**
   * @brief Setup AV logging.
   * @param min_log_level The log level.
   */
  void setup_av_logging(int min_log_level);

  /**
   * @brief Setup logging for libdisplaydevice.
   * @param min_log_level The log level.
   */
  void setup_libdisplaydevice_logging(int min_log_level);
}  // namespace logging
