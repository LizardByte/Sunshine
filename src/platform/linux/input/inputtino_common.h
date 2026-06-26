/**
 * @file src/platform/linux/input/inputtino_common.h
 * @brief Declarations for inputtino common input handling.
 */
#pragma once

// lib includes
#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/linux/input/inputtino_seat.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf {

  /**
   * @brief Append the target seat name to an inputtino device name when needed.
   *
   * @param base_name Base uinput device name.
   * @return Device name scoped to the target seat.
   */
  inline std::string inputtino_name_for_seat(std::string_view base_name) {
    auto seat_id = inputtino_seat::get_target_seat();
    if (seat_id.empty() || seat_id == "seat0") {
      return std::string(base_name);
    }

    std::string name;
    name.reserve(base_name.size() + seat_id.size() + 3);
    name.append(base_name);
    name.append(" (");
    name.append(seat_id);
    name.push_back(')');
    return name;
  }

  /**
   * @brief Variant of inputtino virtual gamepad implementations Sunshine can create.
   */
  using joypads_t = std::variant<inputtino::XboxOneJoypad, inputtino::SwitchJoypad, inputtino::PS5Joypad>;

  /**
   * @brief inputtino joypad collection and its ownership state.
   */
  struct joypad_state {
    std::unique_ptr<joypads_t> joypad;  ///< Active virtual gamepad object for one connected client slot.
    gamepad_feedback_msg_t last_rumble;  ///< Last rumble.
    gamepad_feedback_msg_t last_rgb_led;  ///< Last RGB led.
  };

  /**
   * @brief Global inputtino device handles shared by clients.
   */
  struct input_raw_t {
    input_raw_t():
        mouse(inputtino::Mouse::create({
          .name = inputtino_name_for_seat("Mouse passthrough"sv),
          .vendor_id = 0xBEEF,
          .product_id = 0xDEAD,
          .version = 0x111,
        })),
        keyboard(inputtino::Keyboard::create({
          .name = inputtino_name_for_seat("Keyboard passthrough"sv),
          .vendor_id = 0xBEEF,
          .product_id = 0xDEAD,
          .version = 0x111,
        })),
        gamepads(MAX_GAMEPADS) {
      if (!mouse) {
        BOOST_LOG(warning) << "Unable to create virtual mouse: " << mouse.getErrorMessage();
      }
      if (!keyboard) {
        BOOST_LOG(warning) << "Unable to create virtual keyboard: " << keyboard.getErrorMessage();
      }
    }

    ~input_raw_t() = default;

    // All devices are wrapped in Result because it might be that we aren't able to create them (ex: udev permission denied)
    inputtino::Result<inputtino::Mouse> mouse;  ///< Shared inputtino virtual mouse device.
    inputtino::Result<inputtino::Keyboard> keyboard;  ///< inputtino virtual keyboard device.

    /**
     * A list of gamepads that are currently connected.
     * The pointer is shared because that state will be shared with background threads that deal with rumble and LED
     */
    std::vector<std::shared_ptr<joypad_state>> gamepads;
  };

  /**
   * @brief Per-client inputtino devices for touch and pen input.
   */
  struct client_input_raw_t: public client_input_t {
    /**
     * @brief Create per-client inputtino devices for touch and pen input.
     *
     * @param input Platform input backend that receives the event.
     */
    client_input_raw_t(input_t &input):
        touch(inputtino::TouchScreen::create({
          .name = inputtino_name_for_seat("Touch passthrough"sv),
          .vendor_id = 0xBEEF,
          .product_id = 0xDEAD,
          .version = 0x111,
        })),
        pen(inputtino::PenTablet::create({
          .name = inputtino_name_for_seat("Pen passthrough"sv),
          .vendor_id = 0xBEEF,
          .product_id = 0xDEAD,
          .version = 0x111,
        })) {
      global = (input_raw_t *) input.get();
      if (!touch) {
        BOOST_LOG(warning) << "Unable to create virtual touch screen: " << touch.getErrorMessage();
      }
      if (!pen) {
        BOOST_LOG(warning) << "Unable to create virtual pen tablet: " << pen.getErrorMessage();
      }
    }

    input_raw_t *global;  ///< Shared inputtino device set owned by the global input context.

    // Device state and handles for pen and touch input must be stored in the per-client
    // input context, because each connected client may be sending their own independent
    // pen/touch events. To maintain separation, we expose separate pen and touch devices
    // for each client.
    inputtino::Result<inputtino::TouchScreen> touch;  ///< Per-client virtual touchscreen device.
    inputtino::Result<inputtino::PenTablet> pen;  ///< Per-client virtual pen tablet device.
  };

  /**
   * @brief Convert degrees to radians for controller motion data.
   *
   * @param degree Angle in degrees to convert.
   * @return Angle in radians.
   */
  inline float deg2rad(float degree) {
    return degree * (M_PI / 180.f);
  }
}  // namespace platf
