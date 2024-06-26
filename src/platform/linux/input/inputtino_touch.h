/**
 * @file src/platform/linux/input/inputtino_touch.h
 * @brief Declarations for inputtino touch input handling.
 */
#pragma once

#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

#include "src/platform/common.h"

#include "inputtino_common.h"

using namespace std::literals;

namespace platf::touch {
  void
  update(client_input_raw_t *raw, const touch_port_t &touch_port, const touch_input_t &touch);
}
