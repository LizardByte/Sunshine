/**
 * @file src/platform/linux/input/inputtino_touch.h
 * @brief Declarations for inputtino touch input handling.
 */
#pragma once

// lib includes
#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "inputtino_common.h"
#include "src/platform/common.h"

using namespace std::literals;

namespace platf::touch {
  /**
   * @brief Apply the supplied state update to the platform backend.
   *
   * @param raw Platform-specific input backend state.
   * @param touch_port Touch coordinate bounds used for scaling.
   * @param touch Touch event data to apply to the virtual device.
   */
  void update(client_input_raw_t *raw, const touch_port_t &touch_port, const touch_input_t &touch);
}  // namespace platf::touch
