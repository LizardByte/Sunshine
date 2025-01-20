/**
 * @file src/platform/linux/input/inputtino_keyboard.h
 * @brief Declarations for inputtino keyboard input handling.
 */
#pragma once

// lib includes
#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "inputtino_common.h"

using namespace std::literals;

namespace platf::keyboard {
  void update(input_raw_t *raw, uint16_t modcode, bool release, uint8_t flags);

  void unicode(input_raw_t *raw, char *utf8, int size);
}  // namespace platf::keyboard
