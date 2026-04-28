/**
 * @file src/platform/linux/input/inputtino_seat.h
 * @brief Helpers for multi-seat naming (udev-only).
 */
#pragma once

#include <string>

namespace platf::inputtino_seat {

  /**
   * Determine the target seat for the current Sunshine instance.
   * Returns empty string if no seat could be determined.
   */
  std::string get_target_seat();

}  // namespace platf::inputtino_seat
