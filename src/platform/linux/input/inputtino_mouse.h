/**
 * @file src/platform/linux/input/inputtino_mouse.h
 * @brief Declarations for inputtino mouse input handling.
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

namespace platf::mouse {
  /**
   * @brief Apply a relative pointer movement to the virtual mouse.
   *
   * @param raw Platform-specific input backend state.
   * @param deltaX Horizontal relative movement in client coordinates.
   * @param deltaY Vertical relative movement in client coordinates.
   */
  void move(input_raw_t *raw, int deltaX, int deltaY);

  /**
   * @brief Move the pointer to an absolute client-provided touch coordinate.
   *
   * @param raw Platform-specific input backend state.
   * @param touch_port Touch coordinate bounds used for scaling.
   * @param x Horizontal absolute coordinate from the client.
   * @param y Vertical absolute coordinate from the client.
   */
  void move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y);

  /**
   * @brief Press or release a virtual mouse button.
   *
   * @param raw Platform-specific input backend state.
   * @param button Mouse button identifier to press or release.
   * @param release Whether the key or button event is a release.
   */
  void button(input_raw_t *raw, int button, bool release);

  /**
   * @brief Apply a vertical scroll event to the virtual mouse.
   *
   * @param raw Platform-specific input backend state.
   * @param high_res_distance High-resolution scroll distance reported by the client.
   */
  void scroll(input_raw_t *raw, int high_res_distance);

  /**
   * @brief Apply a horizontal scroll event to the virtual mouse.
   *
   * @param raw Platform-specific input backend state.
   * @param high_res_distance High-resolution scroll distance reported by the client.
   */
  void hscroll(input_raw_t *raw, int high_res_distance);

  /**
   * @brief Return the current virtual pointer location.
   *
   * @param raw Platform-specific input backend state.
   * @return Current virtual pointer location in screen coordinates.
   */
  util::point_t get_location(input_raw_t *raw);
}  // namespace platf::mouse
