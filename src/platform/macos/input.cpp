/**
 * @file src/platform/macos/input.cpp
 * @brief Definitions for libvirtualhid-backed macOS input handling.
 */

// platform includes
#include <ApplicationServices/ApplicationServices.h>

// standard includes
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// local includes
#include "src/config.h"
#include "src/platform/virtualhid_input.h"

namespace platf {

  /**
   * @brief Global libvirtualhid devices shared by clients.
   */
  struct input_raw_t {
    virtualhid::input_context_t virtualhid;  ///< libvirtualhid input context.
  };

  /**
   * @brief Per-client libvirtualhid devices.
   */
  struct client_input_raw_t: client_input_t {
    /**
     * @brief Create per-client libvirtualhid devices.
     *
     * @param input Platform input backend that receives the event.
     */
    explicit client_input_raw_t(input_t &input):
        virtualhid {((input_raw_t *) input.get())->virtualhid} {}

    virtualhid::client_context_t virtualhid;  ///< libvirtualhid client context.
  };

  input_t input() {
    return {new input_raw_t {}};
  }

  std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    return std::make_unique<client_input_raw_t>(input);
  }

  void freeInput(void *p) {
    auto *input = (input_raw_t *) p;
    delete input;
  }

  void move_mouse(input_t &input, int deltaX, int deltaY) {
    virtualhid::move_mouse(((input_raw_t *) input.get())->virtualhid, deltaX, deltaY);
  }

  void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
    virtualhid::abs_mouse(((input_raw_t *) input.get())->virtualhid, touch_port, x, y);
  }

  void button_mouse(input_t &input, int button, bool release) {
    virtualhid::button_mouse(((input_raw_t *) input.get())->virtualhid, button, release);
  }

  void scroll(input_t &input, int high_res_distance) {
    virtualhid::scroll(((input_raw_t *) input.get())->virtualhid, high_res_distance);
  }

  void hscroll(input_t &input, int high_res_distance) {
    virtualhid::hscroll(((input_raw_t *) input.get())->virtualhid, high_res_distance);
  }

  void keyboard_update(input_t &input, std::uint16_t modcode, bool release, std::uint8_t flags) {
    virtualhid::keyboard_update(((input_raw_t *) input.get())->virtualhid, modcode, release, flags);
  }

  void unicode(input_t &input, char *utf8, int size) {
    virtualhid::unicode(((input_raw_t *) input.get())->virtualhid, utf8, size);
  }

  void touch_update(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch) {
    virtualhid::touch_update(((client_input_raw_t *) input)->virtualhid, touch_port, touch);
  }

  void pen_update(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen) {
    virtualhid::pen_update(((client_input_raw_t *) input)->virtualhid, touch_port, pen);
  }

  int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    return virtualhid::alloc_gamepad(((input_raw_t *) input.get())->virtualhid, id, metadata, std::move(feedback_queue));
  }

  void free_gamepad(input_t &input, int nr) {
    virtualhid::free_gamepad(((input_raw_t *) input.get())->virtualhid, nr);
  }

  void gamepad_update(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    virtualhid::gamepad_update(((input_raw_t *) input.get())->virtualhid, nr, gamepad_state);
  }

  void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    virtualhid::gamepad_touch(((input_raw_t *) input.get())->virtualhid, touch);
  }

  void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    virtualhid::gamepad_motion(((input_raw_t *) input.get())->virtualhid, motion);
  }

  void gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    virtualhid::gamepad_battery(((input_raw_t *) input.get())->virtualhid, battery);
  }

  util::point_t get_mouse_loc(input_t & /*input*/) {
    const auto event = CGEventCreate(nullptr);
    if (!event) {
      return {0.0, 0.0};
    }

    const auto current = CGEventGetLocation(event);
    CFRelease(event);
    return {current.x, current.y};
  }

  platform_caps::caps_t get_capabilities() {
    platform_caps::caps_t caps = 0;
    const auto runtime = virtualhid::create_runtime();
    if (!runtime) {
      return caps;
    }

    const auto &capabilities = runtime->capabilities();
    if (capabilities.supports_gamepad && virtualhid::configured_gamepad_supports_touchpad()) {
      caps |= platform_caps::controller_touch;
    }
    if (config::input.native_pen_touch && (capabilities.supports_touchscreen || capabilities.supports_pen_tablet)) {
      caps |= platform_caps::pen_touch;
    }

    return caps;
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    static std::vector<supported_gamepad_t> gamepads;
    if (!input || !input->get()) {
      gamepads = virtualhid::static_supported_gamepads();
      return gamepads;
    }

    const auto raw = (input_raw_t *) input->get();
    gamepads = virtualhid::supported_gamepads(raw->virtualhid.runtime.get());
    return gamepads;
  }

}  // namespace platf
