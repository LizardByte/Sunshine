/**
 * @file src/platform/windows/input.h
 * @brief ViGEm report updates that do not submit input to a device.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <map>

// platform includes
#include <Windows.h>

// lib includes
#include <ViGEm/Client.h>

// local includes
#include "src/platform/common.h"

namespace platf::vigem {
  /**
   * @brief Scale an 8-bit rumble amplitude to the established 16-bit range.
   *
   * @param strength Rumble amplitude reported by ViGEm.
   * @return Amplitude multiplied by 256.
   */
  std::uint16_t scale_rumble(std::uint8_t strength);

  /**
   * @brief Update a DualShock 4 touch report and its pointer allocation state.
   *
   * @param ds4_report Report receiving the touch event.
   * @param pointer_id_map Mapping from client pointer IDs to the two report slots.
   * @param available_pointers Byte mask of unallocated report slots.
   * @param touch Client touch event.
   * @return True when the updated report should be submitted to ViGEm.
   */
  bool update_touch(DS4_REPORT_EX &ds4_report, std::map<std::uint32_t, std::uint8_t> &pointer_id_map, std::byte &available_pointers, const gamepad_touch_t &touch);

  /**
   * @brief Update DualShock 4 battery fields while preserving unknown values.
   *
   * @param ds4_report Report receiving the battery event.
   * @param battery Client battery state and percentage.
   */
  void update_battery(DS4_REPORT_EX &ds4_report, const gamepad_battery_t &battery);
}  // namespace platf::vigem
