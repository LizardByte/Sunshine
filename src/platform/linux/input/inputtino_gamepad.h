/**
 * @file src/platform/linux/input/inputtino_gamepad.h
 * @brief Declarations for inputtino gamepad input handling.
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

namespace platf::gamepad {

  /**
   * @brief Enumerates supported controller type options.
   */
  enum ControllerType {
    XboxOneWired,  ///< Xbox One Wired Controller
    DualSenseWired,  ///< DualSense Wired Controller
    SwitchProWired  ///< Switch Pro Wired Controller
  };

  /**
   * @brief Allocate and initialize platform input state for a stream.
   *
   * @param raw Platform-specific input backend state.
   * @param id Identifier for the controller, session, display, or resource.
   * @param metadata Output structure populated with HDR metadata.
   * @param feedback_queue Feedback queue.
   * @return Allocated object or identifier, or an error value on failure.
   */
  int alloc(input_raw_t *raw, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue);

  /**
   * @brief Release backend resources for the indexed gamepad.
   *
   * @param raw Platform-specific input backend state.
   * @param nr Controller index assigned by the client.
   */
  void free(input_raw_t *raw, int nr);

  /**
   * @brief Apply the supplied state update to the platform backend.
   *
   * @param raw Platform-specific input backend state.
   * @param nr Controller index assigned by the client.
   * @param gamepad_state Gamepad state.
   */
  void update(input_raw_t *raw, int nr, const gamepad_state_t &gamepad_state);

  /**
   * @brief Apply controller touchpad data to the backend device.
   *
   * @param raw Platform-specific input backend state.
   * @param touch Touch event data to apply to the virtual device.
   */
  void touch(input_raw_t *raw, const gamepad_touch_t &touch);

  /**
   * @brief Apply controller motion sensor data to the backend device.
   *
   * @param raw Platform-specific input backend state.
   * @param motion Motion sensor data to apply to the virtual device.
   */
  void motion(input_raw_t *raw, const gamepad_motion_t &motion);

  /**
   * @brief Apply controller battery status to the backend device.
   *
   * @param raw Platform-specific input backend state.
   * @param battery Battery status data reported by the virtual device.
   */
  void battery(input_raw_t *raw, const gamepad_battery_t &battery);

  /**
   * @brief Return gamepad slots supported by the inputtino backend.
   *
   * @param input Platform input backend that receives the event.
   * @return Mutable list of supported virtual gamepads for the input backend.
   */
  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input);
}  // namespace platf::gamepad
