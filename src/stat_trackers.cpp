/**
 * @file src/stat_trackers.cpp
 * @brief todo
 */
#include "stat_trackers.h"

namespace stat_trackers {

  boost::format
  one_digit_after_decimal() {
    return boost::format("%1$.1f");
  }

}  // namespace stat_trackers
