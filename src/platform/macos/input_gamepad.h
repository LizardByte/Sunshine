/**
 * @file src/platform/macos/input_gamepad.h
 * @brief Virtual HID gamepad report layout and the pure state→report mapping.
 *
 * Split out from input.cpp so the mapping logic (button bits, D-pad HAT, axis
 * and trigger scaling) can be unit-tested without creating a real
 * IOHIDUserDevice, which requires a restricted entitlement and cannot run in
 * CI. See tests/unit/platform/macos/test_input.cpp.
 */
#pragma once

// standard includes
#include <cstdint>

// local includes
#include "src/platform/common.h"

namespace platf {

  /**
   * @brief HID input report layout for the virtual gamepad.
   *
   * The field order MUST match the Usage declaration order in the HID report
   * descriptor (kGamepadHIDDescriptor in input.cpp).
   * Total: 1 (report_id) + 2 (buttons 1-11 + 5 pad) + 1 (hat 4b + 4b pad) + 12 (axes) = 16 bytes.
   */
  struct gamepad_hid_report_t {
    uint8_t report_id;  ///< always 1
    uint16_t buttons;  ///< bits 0-10: A B X Y LB RB Back Start Guide LS RS; bits 11-15: padding
    uint8_t hat;  ///< lower nibble: HAT direction (0-7 or 8=null); upper nibble: padding
    int16_t left_x;  ///< Usage X   (0x30) → SDL a0 = Left Stick X
    int16_t left_y;  ///< Usage Y   (0x31) → SDL a1 = Left Stick Y
    int16_t right_x;  ///< Usage Z   (0x32) → SDL a2 = Right Stick X
    int16_t right_y;  ///< Usage Rx  (0x33) → SDL a3 = Right Stick Y
    int16_t r2;  ///< Usage Ry  (0x34) → SDL a4 = Right Trigger
    int16_t l2;  ///< Usage Rz  (0x35) → SDL a5 = Left Trigger
  } __attribute__((packed));

  static_assert(
    sizeof(gamepad_hid_report_t) == 16,
    "gamepad_hid_report_t size mismatch — update the HID descriptor if fields change"
  );

  /**
   * @brief Maps a Moonlight gamepad state onto the virtual gamepad's HID report.
   *
   * Pure function with no device I/O so it can be unit-tested. The returned
   * report has report_id == 1.
   *
   * Buttons (bits 0-10 → HID buttons 1-11) follow the Razer Serval's SDL
   * GameControllerDB entry so SDL/Steam/Wine auto-map correctly. The D-pad is
   * encoded as a HAT switch (0=N..7=NW, 8=null). Stick Y axes are negated to
   * match the HID convention (up = negative); triggers scale 0..255 onto the
   * full signed range -32768..32767.
   *
   * @param state The gamepad state from the client.
   * @return The populated HID report.
   */
  gamepad_hid_report_t map_gamepad_state_to_hid_report(const gamepad_state_t &state);

}  // namespace platf
