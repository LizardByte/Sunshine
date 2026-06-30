/**
 * @file src/platform/linux/input/inputtino_seat.cpp
 * @brief Implementation for multi-seat naming (udev-only).
 */
// lib includes
#include <lizardbyte/common/env.h>

// local includes
#include "inputtino_seat.h"

namespace platf::inputtino_seat {

  std::string get_target_seat() {
    if (std::string seat; lizardbyte::common::get_env("XDG_SEAT", seat) && !seat.empty()) {
      return seat;
    }

    return {};
  }

}  // namespace platf::inputtino_seat
