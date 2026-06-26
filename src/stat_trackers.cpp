/**
 * @file src/stat_trackers.cpp
 * @brief Definitions for streaming statistic tracking.
 */
// local includes
#include "stat_trackers.h"

namespace stat_trackers {

  /**
   * @brief Create a Boost formatter with one fractional digit.
   */
  boost::format one_digit_after_decimal() {
    return boost::format("%1$.1f");
  }

  /**
   * @brief Create a Boost formatter with two fractional digits.
   */
  boost::format two_digits_after_decimal() {
    return boost::format("%1$.2f");
  }

}  // namespace stat_trackers
