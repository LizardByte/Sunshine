/**
 * @file src/platform/linux/input/inputtino_common.h
 * @brief Declarations for inputtino common input handling.
 */
#pragma once

// lib includes
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

  using joypads_t = std::variant<inputtino::XboxOneJoypad, inputtino::SwitchJoypad, inputtino::PS5Joypad>;

  struct joypad_state {
    std::unique_ptr<joypads_t> joypad;
    gamepad_feedback_msg_t last_rumble;
    gamepad_feedback_msg_t last_rgb_led;
  };

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
    inputtino::Result<inputtino::Mouse> mouse;
    inputtino::Result<inputtino::Keyboard> keyboard;

    /**
     * A list of gamepads that are currently connected.
     * The pointer is shared because that state will be shared with background threads that deal with rumble and LED
     */
    std::vector<std::shared_ptr<joypad_state>> gamepads;
  };

  struct client_input_raw_t: public client_input_t {
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

    input_raw_t *global;

    // Device state and handles for pen and touch input must be stored in the per-client
    // input context, because each connected client may be sending their own independent
    // pen/touch events. To maintain separation, we expose separate pen and touch devices
    // for each client.
    inputtino::Result<inputtino::TouchScreen> touch;
    inputtino::Result<inputtino::PenTablet> pen;
  };

  inline float deg2rad(float degree) {
    return degree * (M_PI / 180.f);
  }
}  // namespace platf
