#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf {

  using joypads_t = std::variant<inputtino::XboxOneJoypad, inputtino::SwitchJoypad, inputtino::PS5Joypad>;

  struct joypad_state {
    std::unique_ptr<joypads_t> joypad;
    gamepad_feedback_msg_t last_rumble;
    gamepad_feedback_msg_t last_rgb_led;
  };

  struct input_raw_t {
    input_raw_t():
        mouse(inputtino::Mouse::create({
          .name = "Mouse passthrough",
          .vendor_id = 0xBEEF,
          .product_id = 0xDEAD,
          .version = 0x111,
        })),
        keyboard(inputtino::Keyboard::create({
          .name = "Keyboard passthrough",
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
          .name = "Touch passthrough",
          .vendor_id = 0xBEEF,
          .product_id = 0xDEAD,
          .version = 0x111,
        })),
        pen(inputtino::PenTablet::create({
          .name = "Pen passthrough",
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

  input_t
  input() {
    return { new input_raw_t() };
  }

  std::unique_ptr<client_input_t>
  allocate_client_input_context(input_t &input) {
    return std::make_unique<client_input_raw_t>(input);
  }

  void
  freeInput(void *p) {
    auto *input = (input_raw_t *) p;
    delete input;
  }

  void
  move_mouse(input_t &input, int deltaX, int deltaY) {
    auto raw = (input_raw_t *) input.get();
    if (raw->mouse) {
      (*raw->mouse).move(deltaX, deltaY);
    }
  }

  void
  abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
    auto raw = (input_raw_t *) input.get();
    if (raw->mouse) {
      (*raw->mouse).move_abs(x, y, touch_port.width, touch_port.height);
    }
  }

  void
  button_mouse(input_t &input, int button, bool release) {
    auto raw = (input_raw_t *) input.get();
    if (raw->mouse) {
      inputtino::Mouse::MOUSE_BUTTON btn_type;
      switch (button) {
        case BUTTON_LEFT:
          btn_type = inputtino::Mouse::LEFT;
          break;
        case BUTTON_MIDDLE:
          btn_type = inputtino::Mouse::MIDDLE;
          break;
        case BUTTON_RIGHT:
          btn_type = inputtino::Mouse::RIGHT;
          break;
        case BUTTON_X1:
          btn_type = inputtino::Mouse::SIDE;
          break;
        case BUTTON_X2:
          btn_type = inputtino::Mouse::EXTRA;
          break;
        default:
          BOOST_LOG(warning) << "Unknown mouse button: " << button;
      }
      if (release) {
        (*raw->mouse).release(btn_type);
      }
      else {
        (*raw->mouse).press(btn_type);
      }
    }
  }

  void
  scroll(input_t &input, int high_res_distance) {
    auto raw = (input_raw_t *) input.get();
    if (raw->mouse) {
      (*raw->mouse).vertical_scroll(high_res_distance);
    }
  }

  void
  hscroll(input_t &input, int high_res_distance) {
    auto raw = (input_raw_t *) input.get();
    if (raw->mouse) {
      (*raw->mouse).horizontal_scroll(high_res_distance);
    }
  }

  void
  keyboard(input_t &input, uint16_t modcode, bool release, uint8_t flags) {
    auto raw = (input_raw_t *) input.get();
    if (raw->keyboard) {
      if (release) {
        (*raw->keyboard).release(modcode);
      }
      else {
        (*raw->keyboard).press(modcode);
      }
    }
  }

  /**
   * Takes an UTF-32 encoded string and returns a hex string representation of the bytes (uppercase)
   *
   * ex: ['👱'] = "1F471" // see UTF encoding at https://www.compart.com/en/unicode/U+1F471
   *
   * adapted from: https://stackoverflow.com/a/7639754
   */
  std::string
  to_hex(const std::basic_string<char32_t> &str) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto &ch : str) {
      ss << ch;
    }

    std::string hex_unicode(ss.str());
    std::transform(hex_unicode.begin(), hex_unicode.end(), hex_unicode.begin(), ::toupper);
    return hex_unicode;
  }

  /**
   * A map of linux scan code -> Moonlight keyboard code
   */
  static const std::map<short, short> key_mappings = {
    { KEY_BACKSPACE, 0x08 }, { KEY_TAB, 0x09 }, { KEY_ENTER, 0x0D }, { KEY_LEFTSHIFT, 0x10 },
    { KEY_LEFTCTRL, 0x11 }, { KEY_CAPSLOCK, 0x14 }, { KEY_ESC, 0x1B }, { KEY_SPACE, 0x20 },
    { KEY_PAGEUP, 0x21 }, { KEY_PAGEDOWN, 0x22 }, { KEY_END, 0x23 }, { KEY_HOME, 0x24 },
    { KEY_LEFT, 0x25 }, { KEY_UP, 0x26 }, { KEY_RIGHT, 0x27 }, { KEY_DOWN, 0x28 },
    { KEY_SYSRQ, 0x2C }, { KEY_INSERT, 0x2D }, { KEY_DELETE, 0x2E }, { KEY_0, 0x30 },
    { KEY_1, 0x31 }, { KEY_2, 0x32 }, { KEY_3, 0x33 }, { KEY_4, 0x34 },
    { KEY_5, 0x35 }, { KEY_6, 0x36 }, { KEY_7, 0x37 }, { KEY_8, 0x38 },
    { KEY_9, 0x39 }, { KEY_A, 0x41 }, { KEY_B, 0x42 }, { KEY_C, 0x43 },
    { KEY_D, 0x44 }, { KEY_E, 0x45 }, { KEY_F, 0x46 }, { KEY_G, 0x47 },
    { KEY_H, 0x48 }, { KEY_I, 0x49 }, { KEY_J, 0x4A }, { KEY_K, 0x4B },
    { KEY_L, 0x4C }, { KEY_M, 0x4D }, { KEY_N, 0x4E }, { KEY_O, 0x4F },
    { KEY_P, 0x50 }, { KEY_Q, 0x51 }, { KEY_R, 0x52 }, { KEY_S, 0x53 },
    { KEY_T, 0x54 }, { KEY_U, 0x55 }, { KEY_V, 0x56 }, { KEY_W, 0x57 },
    { KEY_X, 0x58 }, { KEY_Y, 0x59 }, { KEY_Z, 0x5A }, { KEY_LEFTMETA, 0x5B },
    { KEY_RIGHTMETA, 0x5C }, { KEY_KP0, 0x60 }, { KEY_KP1, 0x61 }, { KEY_KP2, 0x62 },
    { KEY_KP3, 0x63 }, { KEY_KP4, 0x64 }, { KEY_KP5, 0x65 }, { KEY_KP6, 0x66 },
    { KEY_KP7, 0x67 }, { KEY_KP8, 0x68 }, { KEY_KP9, 0x69 }, { KEY_KPASTERISK, 0x6A },
    { KEY_KPPLUS, 0x6B }, { KEY_KPMINUS, 0x6D }, { KEY_KPDOT, 0x6E }, { KEY_KPSLASH, 0x6F },
    { KEY_F1, 0x70 }, { KEY_F2, 0x71 }, { KEY_F3, 0x72 }, { KEY_F4, 0x73 },
    { KEY_F5, 0x74 }, { KEY_F6, 0x75 }, { KEY_F7, 0x76 }, { KEY_F8, 0x77 },
    { KEY_F9, 0x78 }, { KEY_F10, 0x79 }, { KEY_F11, 0x7A }, { KEY_F12, 0x7B },
    { KEY_NUMLOCK, 0x90 }, { KEY_SCROLLLOCK, 0x91 }, { KEY_LEFTSHIFT, 0xA0 }, { KEY_RIGHTSHIFT, 0xA1 },
    { KEY_LEFTCTRL, 0xA2 }, { KEY_RIGHTCTRL, 0xA3 }, { KEY_LEFTALT, 0xA4 }, { KEY_RIGHTALT, 0xA5 },
    { KEY_SEMICOLON, 0xBA }, { KEY_EQUAL, 0xBB }, { KEY_COMMA, 0xBC }, { KEY_MINUS, 0xBD },
    { KEY_DOT, 0xBE }, { KEY_SLASH, 0xBF }, { KEY_GRAVE, 0xC0 }, { KEY_LEFTBRACE, 0xDB },
    { KEY_BACKSLASH, 0xDC }, { KEY_RIGHTBRACE, 0xDD }, { KEY_APOSTROPHE, 0xDE }, { KEY_102ND, 0xE2 }
  };

  void
  unicode(input_t &input, char *utf8, int size) {
    auto raw = (input_raw_t *) input.get();
    if (raw->keyboard) {
      /* Reading input text as UTF-8 */
      auto utf8_str = boost::locale::conv::to_utf<wchar_t>(utf8, utf8 + size, "UTF-8");
      /* Converting to UTF-32 */
      auto utf32_str = boost::locale::conv::utf_to_utf<char32_t>(utf8_str);
      /* To HEX string */
      auto hex_unicode = to_hex(utf32_str);
      BOOST_LOG(debug) << "Unicode, typing U+"sv << hex_unicode;

      /* pressing <CTRL> + <SHIFT> + U */
      (*raw->keyboard).press(0xA2);  // LEFTCTRL
      (*raw->keyboard).press(0xA0);  // LEFTSHIFT
      (*raw->keyboard).press(0x55);  // U
      (*raw->keyboard).release(0x55);  // U

      /* input each HEX character */
      for (auto &ch : hex_unicode) {
        auto key_str = "KEY_"s + ch;
        auto keycode = libevdev_event_code_from_name(EV_KEY, key_str.c_str());
        auto wincode = key_mappings.find(keycode);
        if (keycode == -1 || wincode == key_mappings.end()) {
          BOOST_LOG(warning) << "Unicode, unable to find keycode for: "sv << ch;
        }
        else {
          (*raw->keyboard).press(wincode->second);
          (*raw->keyboard).release(wincode->second);
        }
      }

      /* releasing <SHIFT> and <CTRL> */
      (*raw->keyboard).release(0xA0);  // LEFTSHIFT
      (*raw->keyboard).release(0xA2);  // LEFTCTRL
    }
  }

  void
  touch(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch) {
    auto raw = (client_input_raw_t *) input;
    if (raw->touch) {
      switch (touch.eventType) {
        case LI_TOUCH_EVENT_HOVER:
        case LI_TOUCH_EVENT_DOWN:
        case LI_TOUCH_EVENT_MOVE: {
          // Convert our 0..360 range to -90..90 relative to Y axis
          int adjusted_angle = touch.rotation;

          if (adjusted_angle > 90 && adjusted_angle < 270) {
            // Lower hemisphere
            adjusted_angle = 180 - adjusted_angle;
          }

          // Wrap the value if it's out of range
          if (adjusted_angle > 90) {
            adjusted_angle -= 360;
          }
          else if (adjusted_angle < -90) {
            adjusted_angle += 360;
          }
          (*raw->touch).place_finger(touch.pointerId, touch.x, touch.y, touch.pressureOrDistance, adjusted_angle);
          break;
        }
        case LI_TOUCH_EVENT_CANCEL:
        case LI_TOUCH_EVENT_UP:
        case LI_TOUCH_EVENT_HOVER_LEAVE: {
          (*raw->touch).release_finger(touch.pointerId);
          break;
        }
          // TODO: LI_TOUCH_EVENT_CANCEL_ALL
      }
    }
  }

  static inline float
  deg2rad(float degree) {
    return degree * (M_PI / 180.f);
  }

  void
  pen(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen) {
    auto raw = (client_input_raw_t *) input;
    if (raw->pen) {
      // First set the buttons
      (*raw->pen).set_btn(inputtino::PenTablet::PRIMARY, pen.penButtons & LI_PEN_BUTTON_PRIMARY);
      (*raw->pen).set_btn(inputtino::PenTablet::SECONDARY, pen.penButtons & LI_PEN_BUTTON_SECONDARY);
      (*raw->pen).set_btn(inputtino::PenTablet::TERTIARY, pen.penButtons & LI_PEN_BUTTON_TERTIARY);

      // Set the tool
      inputtino::PenTablet::TOOL_TYPE tool;
      switch (pen.toolType) {
        case LI_TOOL_TYPE_PEN:
          tool = inputtino::PenTablet::PEN;
          break;
        case LI_TOOL_TYPE_ERASER:
          tool = inputtino::PenTablet::ERASER;
          break;
        default:
          tool = inputtino::PenTablet::SAME_AS_BEFORE;
          break;
      }

      // Normalize rotation value to 0-359 degree range
      auto rotation = pen.rotation;
      if (rotation != LI_ROT_UNKNOWN) {
        rotation %= 360;
      }

      // Here we receive:
      //  - Rotation: degrees from vertical in Y dimension (parallel to screen, 0..360)
      //  - Tilt: degrees from vertical in Z dimension (perpendicular to screen, 0..90)
      float tilt_x = 0;
      float tilt_y = 0;
      // Convert polar coordinates into Y tilt angles
      if (pen.tilt != LI_TILT_UNKNOWN && rotation != LI_ROT_UNKNOWN) {
        auto rotation_rads = deg2rad(rotation);
        auto tilt_rads = deg2rad(pen.tilt);
        auto r = std::sin(tilt_rads);
        auto z = std::cos(tilt_rads);

        tilt_x = std::atan2(std::sin(-rotation_rads) * r, z) * 180.f / M_PI;
        tilt_y = std::atan2(std::cos(-rotation_rads) * r, z) * 180.f / M_PI;
      }

      (*raw->pen).place_tool(tool,
        pen.x,
        pen.y,
        pen.eventType == LI_TOUCH_EVENT_DOWN ? pen.pressureOrDistance : -1,
        pen.eventType == LI_TOUCH_EVENT_HOVER ? pen.pressureOrDistance : -1,
        tilt_x,
        tilt_y);
    }
  }

  enum ControllerType {
    XboxOneWired,
    DualSenseWired,
    SwitchProWired
  };

  int
  alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    ControllerType selectedGamepadType;

    if (config::input.gamepad == "x360"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (manual selection)"sv;
      selectedGamepadType = XboxOneWired;
    }
    else if (config::input.gamepad == "ds4"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (manual selection)"sv;
      selectedGamepadType = DualSenseWired;
    }
    else if (config::input.gamepad == "switch"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Nintendo Pro controller (manual selection)"sv;
      selectedGamepadType = SwitchProWired;
    }
    else if (metadata.type == LI_CTYPE_XBOX) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = XboxOneWired;
    }
    else if (metadata.type == LI_CTYPE_PS) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = DualSenseWired;
    }
    else if (metadata.type == LI_CTYPE_NINTENDO) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Nintendo Pro controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = SwitchProWired;
    }
    else if (config::input.motion_as_ds4 && (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by motion sensor presence)"sv;
      selectedGamepadType = DualSenseWired;
    }
    else if (config::input.touchpad_as_ds4 && (metadata.capabilities & LI_CCAP_TOUCHPAD)) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by touchpad presence)"sv;
      selectedGamepadType = DualSenseWired;
    }
    else {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (default)"sv;
      selectedGamepadType = XboxOneWired;
    }

    if (selectedGamepadType == XboxOneWired) {
      if (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has motion sensors, but they are not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_TOUCHPAD) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has a touchpad, but it is not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_RGB_LED) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has an RGB LED, but it is not usable when emulating an Xbox 360 controller"sv;
      }
    }
    else if (selectedGamepadType == DualSenseWired) {
      if (!(metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have motion sensors active"sv;
      }
      if (!(metadata.capabilities & LI_CCAP_TOUCHPAD)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have a touchpad"sv;
      }
    }

    auto raw = (input_raw_t *) input.get();
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
      case XboxOneWired: {
        auto xOne = inputtino::XboxOneJoypad::create({ .name = "Sunshine X-Box One (virtual) pad",
          // https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c#L147
          .vendor_id = 0x045E,
          .product_id = 0x02EA,
          .version = 0x0408 });
        if (xOne) {
          (*xOne).set_on_rumble(on_rumble_fn);
          gamepad->joypad = std::make_unique<joypads_t>(std::move(*xOne));
          raw->gamepads[id.globalIndex] = std::move(gamepad);
          return 0;
        }
        else {
          BOOST_LOG(warning) << "Unable to create virtual Xbox One controller: " << xOne.getErrorMessage();
          return -1;
        }
      }
      case SwitchProWired: {
        auto switchPro = inputtino::SwitchJoypad::create({ .name = "Sunshine Nintendo (virtual) pad",
          // https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L981
          .vendor_id = 0x057e,
          .product_id = 0x2009,
          .version = 0x8111 });
        if (switchPro) {
          (*switchPro).set_on_rumble(on_rumble_fn);
          gamepad->joypad = std::make_unique<joypads_t>(std::move(*switchPro));
          raw->gamepads[id.globalIndex] = std::move(gamepad);
          return 0;
        }
        else {
          BOOST_LOG(warning) << "Unable to create virtual Switch Pro controller: " << switchPro.getErrorMessage();
          return -1;
        }
      }
      case DualSenseWired: {
        auto ds5 = inputtino::PS5Joypad::create({ .name = "Sunshine DualSense (virtual) pad", .vendor_id = 0x054C, .product_id = 0x0CE6, .version = 0x8111 });
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

          // Activate the motion sensors
          feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_ACCEL, 100));
          feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_GYRO, 100));

          gamepad->joypad = std::make_unique<joypads_t>(std::move(*ds5));
          raw->gamepads[id.globalIndex] = std::move(gamepad);
          return 0;
        }
        else {
          BOOST_LOG(warning) << "Unable to create virtual DualShock 5 controller: " << ds5.getErrorMessage();
          return -1;
        }
      }
    }
  }

  void
  free_gamepad(input_t &input, int nr) {
    auto raw = (input_raw_t *) input.get();
    // This will call the destructor which in turn will stop the background threads for rumble and LED (and ultimately remove the joypad device)
    raw->gamepads[nr]->joypad.reset();
    raw->gamepads[nr].reset();
  }

  void
  gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    auto raw = (input_raw_t *) input.get();
    auto gamepad = raw->gamepads[nr];
    if (!gamepad) {
      return;
    }

    if (std::holds_alternative<inputtino::PS5Joypad>(*gamepad->joypad)) {
      auto &gc = std::get<inputtino::PS5Joypad>(*gamepad->joypad);
      gc.set_pressed_buttons(gamepad_state.buttonFlags);
      gc.set_stick(inputtino::Joypad::LS, gamepad_state.lsX, gamepad_state.lsY);
      gc.set_stick(inputtino::Joypad::RS, gamepad_state.rsX, gamepad_state.rsY);
      gc.set_triggers(gamepad_state.lt, gamepad_state.rt);
    }
    else if (std::holds_alternative<inputtino::XboxOneJoypad>(*gamepad->joypad)) {
      auto &gc = std::get<inputtino::XboxOneJoypad>(*gamepad->joypad);
      gc.set_pressed_buttons(gamepad_state.buttonFlags);
      gc.set_stick(inputtino::Joypad::LS, gamepad_state.lsX, gamepad_state.lsY);
      gc.set_stick(inputtino::Joypad::RS, gamepad_state.rsX, gamepad_state.rsY);
      gc.set_triggers(gamepad_state.lt, gamepad_state.rt);
    }
    else if (std::holds_alternative<inputtino::SwitchJoypad>(*gamepad->joypad)) {
      auto &gc = std::get<inputtino::SwitchJoypad>(*gamepad->joypad);
      gc.set_pressed_buttons(gamepad_state.buttonFlags);
      gc.set_stick(inputtino::Joypad::LS, gamepad_state.lsX, gamepad_state.lsY);
      gc.set_stick(inputtino::Joypad::RS, gamepad_state.rsX, gamepad_state.rsY);
      gc.set_triggers(gamepad_state.lt, gamepad_state.rt);
    }
  }

  void
  gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    auto raw = (input_raw_t *) input.get();
    auto gamepad = raw->gamepads[touch.id.globalIndex];
    if (!gamepad) {
      return;
    }
    // Only the PS5 controller supports touch input
    if (std::holds_alternative<inputtino::PS5Joypad>(*gamepad->joypad)) {
      if (touch.pressure > 0.5) {
        std::get<inputtino::PS5Joypad>(*gamepad->joypad).place_finger(touch.pointerId, touch.x * inputtino::PS5Joypad::touchpad_width, touch.y * inputtino::PS5Joypad::touchpad_height);
      }
      else {
        std::get<inputtino::PS5Joypad>(*gamepad->joypad).release_finger(touch.pointerId);
      }
    }
  }

  void
  gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    auto raw = (input_raw_t *) input.get();
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
          std::get<inputtino::PS5Joypad>(*gamepad->joypad).set_motion(inputtino::PS5Joypad::GYROSCOPE, motion.x, motion.y, motion.z);
          break;
      }
    }
  }

  void
  gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    auto raw = (input_raw_t *) input.get();
    auto gamepad = raw->gamepads[battery.id.globalIndex];
    if (!gamepad) {
      return;
    }
    // Only the PS5 controller supports motion
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
        case LI_BATTERY_STATE_NOT_CHARGING:
        case LI_BATTERY_PERCENTAGE_UNKNOWN:
        case LI_BATTERY_STATE_UNKNOWN:
        case LI_BATTERY_STATE_NOT_PRESENT:
          state = inputtino::PS5Joypad::CHARGHING_ERROR;
          break;
      }
      std::get<inputtino::PS5Joypad>(*gamepad->joypad).set_battery(state, battery.percentage / 2.55);  // TODO: 255 (0xFF) is 100%?
    }
  }

  platform_caps::caps_t
  get_capabilities() {
    platform_caps::caps_t caps = 0;
    // TODO: if has_uinput
    caps |= platform_caps::pen_touch;

    // We support controller touchpad input as long as we're not emulating X360
    if (config::input.gamepad != "x360"sv) {
      caps |= platform_caps::controller_touch;
    }

    return caps;
  }

  std::vector<std::string_view> &
  supported_gamepads() {
    static std::vector<std::string_view> gps {
      "auto"sv, "x360"sv, "ds4"sv, "ps4"sv, "switch"sv
    };

    return gps;
  }
}  // namespace platf