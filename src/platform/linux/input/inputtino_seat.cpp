/**
 * @file src/platform/linux/input/inputtino_seat.cpp
 * @brief Implementation for multi-seat naming (udev-only).
 */

#include "inputtino_seat.h"

#include <cstdlib>

namespace platf::inputtino_seat {

  std::string get_target_seat() {
    if (const char *seat = std::getenv("XDG_SEAT")) {
      if (seat[0] != '\0') {
        return seat;
      }
    }

    return {};
  }

}  // namespace platf::inputtino_seat
