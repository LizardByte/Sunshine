/**
 * @file src/platform/virtualhid_input.h
 * @brief Declarations for libvirtualhid-backed input helpers.
 */
#pragma once

// standard includes
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// lib includes
#include <libvirtualhid/libvirtualhid.hpp>

// local includes
#include "src/platform/common.h"

namespace platf::virtualhid {

  /**
   * @brief Runtime and virtual devices owned by one platform input context.
   */
  struct input_context_t {
    /**
     * @brief Construct the libvirtualhid input context.
     */
    input_context_t();

    std::unique_ptr<lvh::Runtime> runtime;  ///< libvirtualhid runtime.
    std::unique_ptr<lvh::Keyboard> keyboard;  ///< Shared virtual keyboard.
    std::unique_ptr<lvh::Mouse> mouse;  ///< Shared virtual mouse.
    std::vector<std::shared_ptr<struct gamepad_context_t>> gamepads;  ///< Virtual gamepad slots.
  };

  /**
   * @brief Per-client virtual touch and pen state.
   */
  struct client_context_t {
    /**
     * @brief Create per-client libvirtualhid devices.
     *
     * @param input Global input context.
     */
    explicit client_context_t(input_context_t &input);

    input_context_t *global = nullptr;  ///< Shared global input context.
    std::unique_ptr<lvh::Touchscreen> touch;  ///< Per-client touchscreen.
    std::unique_ptr<lvh::PenTablet> pen;  ///< Per-client pen tablet.
    std::set<std::int32_t> active_touches;  ///< Active touchscreen contacts.
    std::set<lvh::PenButton> pressed_pen_buttons;  ///< Active pen tablet buttons.
  };

  /**
   * @brief Create a platform-default libvirtualhid runtime.
   *
   * @return Runtime instance.
   */
  std::unique_ptr<lvh::Runtime> create_runtime();

  /**
   * @brief Return static gamepad choices for config validation.
   *
   * @return Supported gamepad choices.
   */
  std::vector<supported_gamepad_t> static_supported_gamepads();

  /**
   * @brief Return gamepad choices and runtime availability.
   *
   * @param runtime Runtime to probe.
   * @param fallback_vigem_available Whether Windows ViGEm fallback can create gamepads.
   * @return Supported gamepad choices.
   */
  std::vector<supported_gamepad_t> supported_gamepads(lvh::Runtime *runtime, bool fallback_vigem_available = false);

  /**
   * @brief Allocate a libvirtualhid gamepad.
   *
   * @param context Input context.
   * @param id Sunshine gamepad identifiers.
   * @param metadata Client-reported controller metadata.
   * @param feedback_queue Queue used to return gamepad feedback to the client.
   * @return 0 on success, otherwise -1.
   */
  int alloc_gamepad(input_context_t &context, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue);

  /**
   * @brief Check whether a libvirtualhid gamepad exists in a slot.
   *
   * @param context Input context.
   * @param nr Gamepad slot index.
   * @return True when a virtual gamepad is active.
   */
  bool has_gamepad(const input_context_t &context, int nr);

  /**
   * @brief Release a libvirtualhid gamepad slot.
   *
   * @param context Input context.
   * @param nr Gamepad slot index.
   */
  void free_gamepad(input_context_t &context, int nr);

  /**
   * @brief Submit a full gamepad state.
   *
   * @param context Input context.
   * @param nr Gamepad slot index.
   * @param state Sunshine gamepad state.
   */
  void gamepad_update(input_context_t &context, int nr, const gamepad_state_t &state);

  /**
   * @brief Submit a gamepad touch event.
   *
   * @param context Input context.
   * @param touch Sunshine touch event.
   */
  void gamepad_touch(input_context_t &context, const gamepad_touch_t &touch);

  /**
   * @brief Submit a gamepad motion event.
   *
   * @param context Input context.
   * @param motion Sunshine motion event.
   */
  void gamepad_motion(input_context_t &context, const gamepad_motion_t &motion);

  /**
   * @brief Submit gamepad battery metadata.
   *
   * @param context Input context.
   * @param battery Sunshine battery event.
   */
  void gamepad_battery(input_context_t &context, const gamepad_battery_t &battery);

  /**
   * @brief Move the virtual mouse relatively.
   *
   * @param context Input context.
   * @param delta_x Horizontal delta.
   * @param delta_y Vertical delta.
   */
  void move_mouse(input_context_t &context, int delta_x, int delta_y);

  /**
   * @brief Move the virtual mouse absolutely inside a target touch port.
   *
   * @param context Input context.
   * @param touch_port Target coordinate space.
   * @param x Absolute X coordinate.
   * @param y Absolute Y coordinate.
   */
  void abs_mouse(input_context_t &context, const touch_port_t &touch_port, float x, float y);

  /**
   * @brief Submit a mouse button event.
   *
   * @param context Input context.
   * @param button Moonlight mouse button.
   * @param release Whether the button was released.
   */
  void button_mouse(input_context_t &context, int button, bool release);

  /**
   * @brief Submit vertical scroll input.
   *
   * @param context Input context.
   * @param high_res_distance High-resolution scroll distance.
   */
  void scroll(input_context_t &context, int high_res_distance);

  /**
   * @brief Submit horizontal scroll input.
   *
   * @param context Input context.
   * @param high_res_distance High-resolution scroll distance.
   */
  void hscroll(input_context_t &context, int high_res_distance);

  /**
   * @brief Submit a keyboard key transition.
   *
   * @param context Input context.
   * @param modcode Portable key code.
   * @param release Whether the key was released.
   */
  void keyboard_update(input_context_t &context, std::uint16_t modcode, bool release);

  /**
   * @brief Submit UTF-8 text input.
   *
   * @param context Input context.
   * @param utf8 UTF-8 text buffer.
   * @param size Text buffer size.
   */
  void unicode(input_context_t &context, const char *utf8, int size);

  /**
   * @brief Submit a touchscreen event.
   *
   * @param context Client context.
   * @param touch Touch event.
   */
  void touch_update(client_context_t &context, const touch_input_t &touch);

  /**
   * @brief Submit a pen event.
   *
   * @param context Client context.
   * @param pen Pen event.
   */
  void pen_update(client_context_t &context, const pen_input_t &pen);

  /**
   * @brief Return whether the configured gamepad profile can expose touchpad input.
   *
   * @return True when controller touchpad input should be advertised.
   */
  bool configured_gamepad_supports_touchpad();

}  // namespace platf::virtualhid
