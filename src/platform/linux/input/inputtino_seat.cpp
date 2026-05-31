/**
 * @file src/platform/linux/input/inputtino_seat.cpp
 * @brief Implementation for multi-seat naming (udev-only).
 */
// standard includes
#include <cstdlib>

// local includes
#include "inputtino_seat.h"
#include "src/platform/common.h"

namespace platf::inputtino_seat {

  std::string get_target_seat() {
    if (std::string seat; platf::get_env("XDG_SEAT", seat) && !seat.empty()) {
      return seat;
    }
    return {};
  }

}  // namespace platf::inputtino_seat
