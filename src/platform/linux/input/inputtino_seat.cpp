/**
 * @file src/platform/linux/input/inputtino_seat.cpp
 * @brief Implementation for multi-seat naming (udev-only).
 */
// standard includes
#include <cstdlib>

// local includes
#include "inputtino_seat.h"

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
