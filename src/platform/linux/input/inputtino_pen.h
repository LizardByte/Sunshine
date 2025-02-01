/**
 * @file src/platform/linux/input/inputtino_pen.h
 * @brief Declarations for inputtino pen input handling.
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

namespace platf::pen {
  void update(client_input_raw_t *raw, const touch_port_t &touch_port, const pen_input_t &pen);
}
