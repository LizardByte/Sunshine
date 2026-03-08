/**
 * @file src/platform/linux/input/inputtino_gamepad.cpp
 * @brief Definitions for inputtino gamepad input handling.
 */
// lib includes
#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "inputtino_common.h"
#include "inputtino_gamepad.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf::gamepad {

  enum GamepadStatus {
    UHID_NOT_AVAILABLE = 0,  ///< UHID is not available
    UINPUT_NOT_AVAILABLE,  ///< UINPUT is not available
    XINPUT_NOT_AVAILABLE,  ///< XINPUT is not available
    GAMEPAD_STATUS  ///< Helper to indicate the number of status
  };

  auto create_xbox_one() {
    return inputtino::XboxOneJoypad::create({.name = "Sunshine X-Box One (virtual) pad",
                                             // https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c#L147
                                             .vendor_id = 0x045E,
                                             .product_id = 0x02EA,
                                             .version = 0x0408});
  }

  auto create_switch() {
    return inputtino::SwitchJoypad::create({.name = "Sunshine Nintendo (virtual) pad",
                                            // https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L981
                                            .vendor_id = 0x057e,
                                            .product_id = 0x2009,
                                            .version = 0x8111});
  }

  auto create_ds5(int globalIndex) {
    std::string device_mac = "";  // Inputtino checks empty() to generate a random MAC

    if (!config::input.ds5_inputtino_randomize_mac && globalIndex >= 0 && globalIndex <= 255) {
      // Generate private virtual device MAC based on gamepad globalIndex between 0 (00) and 255 (ff)
      device_mac = std::format("02:00:00:00:00:{:02x}", globalIndex);
    }

    return inputtino::PS5Joypad::create({.name = "Sunshine PS5 (virtual) pad", .vendor_id = 0x054C, .product_id = 0x0CE6, .version = 0x8111, .device_phys = device_mac, .device_uniq = device_mac});
  }

  int alloc(input_raw_t *raw, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    ControllerType selectedGamepadType;

    if (config::input.gamepad == "xone"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox One controller (manual selection)"sv;
      selectedGamepadType = XboxOneWired;
    } else if (config::input.gamepad == "ds5"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualSense 5 controller (manual selection)"sv;
      selectedGamepadType = DualSenseWired;
    } else if (config::input.gamepad == "switch"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Nintendo Pro controller (manual selection)"sv;
      selectedGamepadType = SwitchProWired;
    } else if (metadata.type == LI_CTYPE_XBOX) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox One controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = XboxOneWired;
    } else if (metadata.type == LI_CTYPE_PS) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 5 controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = DualSenseWired;
    } else if (metadata.type == LI_CTYPE_NINTENDO) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Nintendo Pro controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = SwitchProWired;
    } else if (config::input.motion_as_ds4 && (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 5 controller (auto-selected by motion sensor presence)"sv;
      selectedGamepadType = DualSenseWired;
    } else if (config::input.touchpad_as_ds4 && (metadata.capabilities & LI_CCAP_TOUCHPAD)) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 5 controller (auto-selected by touchpad presence)"sv;
      selectedGamepadType = DualSenseWired;
    } else {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox One controller (default)"sv;
      selectedGamepadType = XboxOneWired;
    }

    if (selectedGamepadType == XboxOneWired || selectedGamepadType == SwitchProWired) {
      if (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has motion sensors, but they are not usable when emulating a joypad different from DS5"sv;
      }
      if (metadata.capabilities & LI_CCAP_TOUCHPAD) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has a touchpad, but it is not usable when emulating a joypad different from DS5"sv;
      }
      if (metadata.capabilities & LI_CCAP_RGB_LED) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has an RGB LED, but it is not usable when emulating a joypad different from DS5"sv;
      }
    } else if (selectedGamepadType == DualSenseWired) {
      if (!(metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 5 controller, but the client gamepad doesn't have motion sensors active"sv;
      }
      if (!(metadata.capabilities & LI_CCAP_TOUCHPAD)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 5 controller, but the client gamepad doesn't have a touchpad"sv;
      }
    }

    auto gamepad = std::make_shared<joypad_state>(joypad_state {});
    auto on_rumble_fn = [feedback_queue, idx = id.clientRelativeIndex, gamepad](int low_freq, int high_freq) {
      // Don't resend duplicate rumble data
      if (gamepad->last_rumble.type == platf::gamepad_feedback_e::rumble && gamepad->last_rumble.data.rumble.lowfreq == low_freq && gamepad->last_rumble.data.rumble.highfreq == high_freq) {
        return;
      }

      gamepad_feedback_msg_t msg = gamepad_feedback_msg_t::make_rumble(idx, low_freq, high_freq);
      feedback_queue->raise(msg);
      gamepad->last_rumble = msg;
    };

    switch (selectedGamepadType) {
      case XboxOneWired:
        {
          auto xOne = create_xbox_one();
          if (xOne) {
            (*xOne).set_on_rumble(on_rumble_fn);
            gamepad->joypad = std::make_unique<joypads_t>(std::move(*xOne));
            raw->gamepads[id.globalIndex] = std::move(gamepad);
            return 0;
          } else {
            BOOST_LOG(warning) << "Unable to create virtual Xbox One controller: " << xOne.getErrorMessage();
            return -1;
          }
        }
      case SwitchProWired:
        {
          auto switchPro = create_switch();
          if (switchPro) {
            (*switchPro).set_on_rumble(on_rumble_fn);
            gamepad->joypad = std::make_unique<joypads_t>(std::move(*switchPro));
            raw->gamepads[id.globalIndex] = std::move(gamepad);
            return 0;
          } else {
            BOOST_LOG(warning) << "Unable to create virtual Switch Pro controller: " << switchPro.getErrorMessage();
            return -1;
          }
        }
      case DualSenseWired:
        {
          auto ds5 = create_ds5(id.globalIndex);
          if (ds5) {
            (*ds5).set_on_rumble(on_rumble_fn);
            (*ds5).set_on_led([feedback_queue, idx = id.clientRelativeIndex, gamepad](int r, int g, int b) {
              // Don't resend duplicate LED data
              if (gamepad->last_rgb_led.type == platf::gamepad_feedback_e::set_rgb_led && gamepad->last_rgb_led.data.rgb_led.r == r && gamepad->last_rgb_led.data.rgb_led.g == g && gamepad->last_rgb_led.data.rgb_led.b == b) {
                return;
              }

              auto msg = gamepad_feedback_msg_t::make_rgb_led(idx, r, g, b);
              feedback_queue->raise(msg);
              gamepad->last_rgb_led = msg;
            });

            (*ds5).set_on_trigger_effect([feedback_queue, idx = id.clientRelativeIndex](const inputtino::PS5Joypad::TriggerEffect &trigger_effect) {
              feedback_queue->raise(gamepad_feedback_msg_t::make_adaptive_triggers(idx, trigger_effect.event_flags, trigger_effect.type_left, trigger_effect.type_right, trigger_effect.left, trigger_effect.right));
            });

            // Activate the motion sensors
            feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_ACCEL, 100));
            feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_GYRO, 100));

            gamepad->joypad = std::make_unique<joypads_t>(std::move(*ds5));
            raw->gamepads[id.globalIndex] = std::move(gamepad);
            return 0;
          } else {
            BOOST_LOG(warning) << "Unable to create virtual DualShock 5 controller: " << ds5.getErrorMessage();
            return -1;
          }
        }
    }
    return -1;
  }

  void free(input_raw_t *raw, int nr) {
    // This will call the destructor which in turn will stop the background threads for rumble and LED (and ultimately remove the joypad device)
    raw->gamepads[nr]->joypad.reset();
    raw->gamepads[nr].reset();
  }

  void update(input_raw_t *raw, int nr, const gamepad_state_t &gamepad_state) {
    auto gamepad = raw->gamepads[nr];
    if (!gamepad) {
      return;
    }

    std::visit([gamepad_state](inputtino::Joypad &gc) {
      gc.set_pressed_buttons(gamepad_state.buttonFlags);
      gc.set_stick(inputtino::Joypad::LS, gamepad_state.lsX, gamepad_state.lsY);
      gc.set_stick(inputtino::Joypad::RS, gamepad_state.rsX, gamepad_state.rsY);
      gc.set_triggers(gamepad_state.lt, gamepad_state.rt);
    },
               *gamepad->joypad);
  }

  void touch(input_raw_t *raw, const gamepad_touch_t &touch) {
    auto gamepad = raw->gamepads[touch.id.globalIndex];
    if (!gamepad) {
      return;
    }
    // Only the PS5 controller supports touch input
    if (std::holds_alternative<inputtino::PS5Joypad>(*gamepad->joypad)) {
      if (touch.pressure > 0.5) {
        std::get<inputtino::PS5Joypad>(*gamepad->joypad).place_finger(touch.pointerId, touch.x * inputtino::PS5Joypad::touchpad_width, touch.y * inputtino::PS5Joypad::touchpad_height);
      } else {
        std::get<inputtino::PS5Joypad>(*gamepad->joypad).release_finger(touch.pointerId);
      }
    }
  }

  void motion(input_raw_t *raw, const gamepad_motion_t &motion) {
    auto gamepad = raw->gamepads[motion.id.globalIndex];
    if (!gamepad) {
      return;
    }
    // Only the PS5 controller supports motion
    if (std::holds_alternative<inputtino::PS5Joypad>(*gamepad->joypad)) {
      switch (motion.motionType) {
        case LI_MOTION_TYPE_ACCEL:
          std::get<inputtino::PS5Joypad>(*gamepad->joypad).set_motion(inputtino::PS5Joypad::ACCELERATION, motion.x, motion.y, motion.z);
          break;
        case LI_MOTION_TYPE_GYRO:
          std::get<inputtino::PS5Joypad>(*gamepad->joypad).set_motion(inputtino::PS5Joypad::GYROSCOPE, deg2rad(motion.x), deg2rad(motion.y), deg2rad(motion.z));
          break;
      }
    }
  }

  void battery(input_raw_t *raw, const gamepad_battery_t &battery) {
    auto gamepad = raw->gamepads[battery.id.globalIndex];
    if (!gamepad) {
      return;
    }
    // Only the PS5 controller supports battery reports
    if (std::holds_alternative<inputtino::PS5Joypad>(*gamepad->joypad)) {
      inputtino::PS5Joypad::BATTERY_STATE state;
      switch (battery.state) {
        case LI_BATTERY_STATE_CHARGING:
          state = inputtino::PS5Joypad::BATTERY_CHARGHING;
          break;
        case LI_BATTERY_STATE_DISCHARGING:
          state = inputtino::PS5Joypad::BATTERY_DISCHARGING;
          break;
        case LI_BATTERY_STATE_FULL:
          state = inputtino::PS5Joypad::BATTERY_FULL;
          break;
        case LI_BATTERY_STATE_UNKNOWN:
        case LI_BATTERY_STATE_NOT_PRESENT:
        default:
          return;
      }
      if (battery.percentage != LI_BATTERY_PERCENTAGE_UNKNOWN) {
        std::get<inputtino::PS5Joypad>(*gamepad->joypad).set_battery(state, battery.percentage);
      }
    }
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    if (!input) {
      static std::vector gps {
        supported_gamepad_t {"auto", true, ""},
        supported_gamepad_t {"xone", false, ""},
        supported_gamepad_t {"ds5", false, ""},
        supported_gamepad_t {"switch", false, ""},
      };

      return gps;
    }

    auto ds5 = create_ds5(-1);  // Index -1 will result in a random MAC virtual device, which is fine for probing
    auto switchPro = create_switch();
    auto xOne = create_xbox_one();

    static std::vector gps {
      supported_gamepad_t {"auto", true, ""},
      supported_gamepad_t {"xone", static_cast<bool>(xOne), !xOne ? xOne.getErrorMessage() : ""},
      supported_gamepad_t {"ds5", static_cast<bool>(ds5), !ds5 ? ds5.getErrorMessage() : ""},
      supported_gamepad_t {"switch", static_cast<bool>(switchPro), !switchPro ? switchPro.getErrorMessage() : ""},
    };

    for (auto &[name, is_enabled, reason_disabled] : gps) {
      if (!is_enabled) {
        BOOST_LOG(warning) << "Gamepad " << name << " is disabled due to " << reason_disabled;
      }
    }

    return gps;
  }
}  // namespace platf::gamepad
