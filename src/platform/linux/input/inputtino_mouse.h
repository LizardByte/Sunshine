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
  void move(input_raw_t *raw, int deltaX, int deltaY);

  void move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y);

  void button(input_raw_t *raw, int button, bool release);

  void scroll(input_raw_t *raw, int high_res_distance);

  void hscroll(input_raw_t *raw, int high_res_distance);

  util::point_t get_location(input_raw_t *raw);
}  // namespace platf::mouse
