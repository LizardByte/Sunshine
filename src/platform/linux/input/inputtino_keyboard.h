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
  /**
   * @brief Apply the supplied state update to the platform backend.
   *
   * @param raw Platform-specific input backend state.
   * @param modcode Modifier key code to update.
   * @param release Whether the key or button event is a release.
   * @param flags Bit flags that modify the requested operation.
   */
  void update(input_raw_t *raw, uint16_t modcode, bool release, uint8_t flags);

  /**
   * @brief Submit UTF-8 text input to the keyboard backend.
   *
   * @param raw Platform-specific input backend state.
   * @param utf8 UTF-8 text submitted by the client.
   * @param size Number of bytes or elements requested.
   */
  void unicode(input_raw_t *raw, char *utf8, int size);
}  // namespace platf::keyboard
