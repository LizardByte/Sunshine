/**
 * @file src/platform/linux/input/virtualhid.cpp
 * @brief Definitions for libvirtualhid Unix input handling.
 */

// standard includes
#include <utility>

// local includes
#include "src/platform/virtualhid_input.h"

using namespace std::literals;

namespace platf {
  namespace {

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

  }  // namespace

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

  /**
   * @brief Press or release a virtual keyboard key.
   *
   * @param input Platform input backend that receives the event.
   * @param modcode Modifier key code to update.
   * @param release Whether the key or button event is a release.
   * @param flags Bit flags that modify the requested operation; ignored by this backend.
   */
  void keyboard_update(input_t &input, std::uint16_t modcode, bool release, [[maybe_unused]] std::uint8_t flags) {
    virtualhid::keyboard_update(((input_raw_t *) input.get())->virtualhid, modcode, release);
  }

  void unicode(input_t &input, char *utf8, int size) {
    virtualhid::unicode(((input_raw_t *) input.get())->virtualhid, utf8, size);
  }

  void touch_update(client_input_t *input, const touch_port_t & /*touch_port*/, const touch_input_t &touch) {
    virtualhid::touch_update(((client_input_raw_t *) input)->virtualhid, touch);
  }

  void pen_update(client_input_t *input, const touch_port_t & /*touch_port*/, const pen_input_t &pen) {
    virtualhid::pen_update(((client_input_raw_t *) input)->virtualhid, pen);
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

  platform_caps::caps_t get_capabilities() {
    platform_caps::caps_t caps = 0;
    const auto runtime = virtualhid::create_runtime();
    if (!runtime) {
      return caps;
    }

    const auto &capabilities = runtime->capabilities();
    if (config::input.native_pen_touch && (capabilities.supports_touchscreen || capabilities.supports_pen_tablet)) {
      caps |= platform_caps::pen_touch;
    }
    if (virtualhid::configured_gamepad_supports_touchpad()) {
      caps |= platform_caps::controller_touch;
    }

    return caps;
  }

  util::point_t get_mouse_loc(input_t & /*input*/) {
    return {0, 0};
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
