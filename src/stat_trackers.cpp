/**
 * @file src/stat_trackers.cpp
 * @brief Definitions for streaming statistic tracking.
 */
#include "stat_trackers.h"

namespace stat_trackers {

  boost::format
  one_digit_after_decimal() {
    return boost::format("%1$.1f");
  }

  boost::format
  two_digits_after_decimal() {
    return boost::format("%1$.2f");
  }

}  // namespace stat_trackers
