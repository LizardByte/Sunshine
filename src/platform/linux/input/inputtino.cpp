/**
 * @file src/platform/linux/input/inputtino.cpp
 * @brief Definitions for the inputtino Linux input handling.
 */
// lib includes
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "inputtino_common.h"
#include "inputtino_gamepad.h"
#include "inputtino_keyboard.h"
#include "inputtino_mouse.h"
#include "inputtino_pen.h"
#include "inputtino_touch.h"
#include "src/config.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf {

  /**
   * @brief Create the platform input backend for a stream.
   */
  input_t input() {
    return {new input_raw_t()};
  }

  std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    return std::make_unique<client_input_raw_t>(input);
  }

  /**
   * @brief Release a platform input backend created by input().
   */
  void freeInput(void *p) {
    auto *input = (input_raw_t *) p;
    delete input;
  }

  /**
   * @brief Move mouse using the backend coordinate system.
   */
  void move_mouse(input_t &input, int deltaX, int deltaY) {
    auto raw = (input_raw_t *) input.get();
    platf::mouse::move(raw, deltaX, deltaY);
  }

  /**
   * @brief Move the pointer to an absolute client-provided touch coordinate.
   */
  void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
    auto raw = (input_raw_t *) input.get();
    platf::mouse::move_abs(raw, touch_port, x, y);
  }

  /**
   * @brief Press or release a virtual mouse button.
   */
  void button_mouse(input_t &input, int button, bool release) {
    auto raw = (input_raw_t *) input.get();
    platf::mouse::button(raw, button, release);
  }

  /**
   * @brief Apply a vertical scroll event to the virtual mouse.
   */
  void scroll(input_t &input, int high_res_distance) {
    auto raw = (input_raw_t *) input.get();
    platf::mouse::scroll(raw, high_res_distance);
  }

  /**
   * @brief Apply a horizontal scroll event to the virtual mouse.
   */
  void hscroll(input_t &input, int high_res_distance) {
    auto raw = (input_raw_t *) input.get();
    platf::mouse::hscroll(raw, high_res_distance);
  }

  /**
   * @brief Press or release a virtual keyboard key.
   */
  void keyboard_update(input_t &input, uint16_t modcode, bool release, uint8_t flags) {
    auto raw = (input_raw_t *) input.get();
    platf::keyboard::update(raw, modcode, release, flags);
  }

  /**
   * @brief Submit UTF-8 text input to the keyboard backend.
   */
  void unicode(input_t &input, char *utf8, int size) {
    auto raw = (input_raw_t *) input.get();
    platf::keyboard::unicode(raw, utf8, size);
  }

  void touch_update(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch) {
    auto raw = (client_input_raw_t *) input;
    platf::touch::update(raw, touch_port, touch);
  }

  void pen_update(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen) {
    auto raw = (client_input_raw_t *) input;
    platf::pen::update(raw, touch_port, pen);
  }

  int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    auto raw = (input_raw_t *) input.get();
    return platf::gamepad::alloc(raw, id, metadata, feedback_queue);
  }

  /**
   * @brief Release gamepad resources.
   */
  void free_gamepad(input_t &input, int nr) {
    auto raw = (input_raw_t *) input.get();
    platf::gamepad::free(raw, nr);
  }

  void gamepad_update(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    auto raw = (input_raw_t *) input.get();
    platf::gamepad::update(raw, nr, gamepad_state);
  }

  void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    auto raw = (input_raw_t *) input.get();
    platf::gamepad::touch(raw, touch);
  }

  void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    auto raw = (input_raw_t *) input.get();
    platf::gamepad::motion(raw, motion);
  }

  void gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    auto raw = (input_raw_t *) input.get();
    platf::gamepad::battery(raw, battery);
  }

  platform_caps::caps_t get_capabilities() {
    platform_caps::caps_t caps = 0;
    // TODO: if has_uinput
    caps |= platform_caps::pen_touch;

    // We support controller touchpad input only when emulating the PS5 controller
    if (config::input.gamepad == "ds5"sv || config::input.gamepad == "auto"sv) {
      caps |= platform_caps::controller_touch;
    }

    return caps;
  }

  util::point_t get_mouse_loc(input_t &input) {
    auto raw = (input_raw_t *) input.get();
    return platf::mouse::get_location(raw);
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    return platf::gamepad::supported_gamepads(input);
  }
}  // namespace platf
