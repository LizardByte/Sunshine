#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <cstring>
#include <filesystem>

#include "sunshine/platform/common.h"
#include "sunshine/main.h"
#include "sunshine/utility.h"

// Support older versions
#ifndef REL_HWHEEL_HI_RES
#define REL_HWHEEL_HI_RES 0x0c
#endif

#ifndef REL_WHEEL_HI_RES
#define REL_WHEEL_HI_RES 0x0b
#endif

namespace platf {
using namespace std::literals;
using evdev_t = util::safe_ptr<libevdev, libevdev_free>;
using uinput_t = util::safe_ptr<libevdev_uinput, libevdev_uinput_destroy>;

using keyboard_t = util::safe_ptr_v2<Display, int, XCloseDisplay>;

struct input_raw_t {
public:
  void clear_mouse() {
    std::filesystem::path mouse_path { "sunshine_mouse"sv };

    if(std::filesystem::is_symlink(mouse_path)) {
      std::filesystem::remove(mouse_path);
    }

    mouse_input.reset();
  }

  void clear_gamepad(int nr) {
    std::stringstream ss;

    ss << "sunshine_gamepad_"sv << nr;

    std::filesystem::path gamepad_path { ss.str() };
    if(std::filesystem::is_symlink(gamepad_path)) {
      std::filesystem::remove(gamepad_path);
    }

    gamepads[nr] = std::make_pair(uinput_t{}, gamepad_state_t {});
  }

  int create_mouse() {
    libevdev_uinput *buf {};
    int err = libevdev_uinput_create_from_device(mouse_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &buf);
    mouse_input.reset(buf);

    if(err) {
      BOOST_LOG(error) << "Could not create Sunshine Mouse: "sv << strerror(-err);
      return -1;
    }

    std::filesystem::create_symlink(libevdev_uinput_get_devnode(mouse_input.get()), "sunshine_mouse"sv);

    return 0;
  }

  int alloc_gamepad(int nr) {
    TUPLE_2D_REF(input, gamepad_state, gamepads[nr]);

    libevdev_uinput *buf;
    int err = libevdev_uinput_create_from_device(gamepad_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &buf);

    input.reset(buf);
    gamepad_state = gamepad_state_t {};

    if(err) {
      BOOST_LOG(error) << "Could not create Sunshine Gamepad: "sv << strerror(-err);
      return -1;
    }

    std::stringstream ss;
    ss << "sunshine_gamepad_"sv << nr;
    std::filesystem::path gamepad_path { ss.str() };

    if(std::filesystem::is_symlink(gamepad_path)) {
      std::filesystem::remove(gamepad_path);
    }

    std::filesystem::create_symlink(libevdev_uinput_get_devnode(input.get()), gamepad_path);
    return 0;
  }

  void clear() {
    clear_mouse();
    for(int x = 0; x < gamepads.size(); ++x) {
      clear_gamepad(x);
    }
  }

  ~input_raw_t() {
    clear();
  }

  evdev_t gamepad_dev;

  std::vector<std::pair<uinput_t, gamepad_state_t>> gamepads;

  evdev_t mouse_dev;
  uinput_t mouse_input;

  keyboard_t keyboard;
};

void move_mouse(input_t &input, int deltaX, int deltaY) {
  auto mouse = ((input_raw_t*)input.get())->mouse_input.get();

  if(deltaX) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_X, deltaX);
  }

  if(deltaY) {
    libevdev_uinput_write_event(mouse, EV_REL, REL_Y, deltaY);
  }

  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void button_mouse(input_t &input, int button, bool release) {
  int btn_type;
  int scan;

  if(button == 1) {
    btn_type = BTN_LEFT;
    scan = 90001;
  }
  else if(button == 2) {
    btn_type = BTN_MIDDLE;
    scan = 90003;
  }
  else if(button == 3) {
    btn_type = BTN_RIGHT;
    scan = 90002;
  }
  else if(button == 4) {
    btn_type = BTN_SIDE;
    scan = 90004;
  }
  else {
    btn_type = BTN_EXTRA;
    scan = 90005;
  }

  auto mouse = ((input_raw_t*)input.get())->mouse_input.get();
  libevdev_uinput_write_event(mouse, EV_MSC, MSC_SCAN, scan);
  libevdev_uinput_write_event(mouse, EV_KEY, btn_type, release ? 0 : 1);
  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void scroll(input_t &input, int high_res_distance) {
  int distance = high_res_distance / 120;

  auto mouse = ((input_raw_t*)input.get())->mouse_input.get();
  libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL, distance);
  libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL_HI_RES, high_res_distance);
  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

uint16_t keysym(uint16_t modcode) {
  constexpr auto VK_NUMPAD = 0x60;
  constexpr auto VK_F1 = 0x70;

  if(modcode >= VK_NUMPAD && modcode < VK_NUMPAD + 10) {
    return XK_KP_0 + (modcode - VK_NUMPAD);
  }

  if(modcode >= VK_F1 && modcode < VK_F1 + 13) {
    return XK_F1 + (modcode - VK_F1);
  }


  switch(modcode) {
    case 0x08:
      return XK_BackSpace;
    case 0x09:
      return XK_Tab;
    case 0x0D:
      return XK_Return;
    case 0x13:
      return XK_Pause;
    case 0x14:
      return XK_Caps_Lock;
    case 0x1B:
      return XK_Escape;
    case 0x21:
      return XK_Page_Up;
    case 0x22:
      return XK_Page_Down;
    case 0x23:
      return XK_End;
    case 0x24:
      return XK_Home;
    case 0x25:
      return XK_Left;
    case 0x26:
      return XK_Up;
    case 0x27:
      return XK_Right;
    case 0x28:
      return XK_Down;
    case 0x29:
      return XK_Select;
    case 0x2B:
      return XK_Execute;
    case 0x2C:
      return XK_Print;
    case 0x2D:
      return XK_Insert;
    case 0x2E:
      return XK_Delete;
    case 0x2F:
      return XK_Help;
    case 0x6A:
      return XK_KP_Multiply;
    case 0x6B:
      return XK_KP_Add;
    case 0x6C:
      return XK_KP_Separator;
    case 0x6D:
      return XK_KP_Subtract;
    case 0x6E:
      return XK_KP_Decimal;
    case 0x6F:
      return XK_KP_Divide;
    case 0x90:
      return XK_Num_Lock;
    case 0x91:
      return XK_Scroll_Lock;
    case 0xA0:
      return XK_Shift_L;
    case 0xA1:
      return XK_Shift_R;
    case 0xA2:
      return XK_Control_L;
    case 0xA3:
      return XK_Control_R;
    case 0xA4:
      return XK_Alt_L;
    case 0xA5: /* return XK_Alt_R; */
      return XK_Super_L;
    case 0x5B:
      return XK_Super_L;
    case 0x5C:
      return XK_Super_R;
    case 0xBA:
      return XK_semicolon;
    case 0xBB:
      return XK_equal;
    case 0xBC:
      return XK_comma;
    case 0xBD:
      return XK_minus;
    case 0xBE:
      return XK_period;
    case 0xBF:
      return XK_slash;
    case 0xC0:
      return XK_grave;
    case 0xDB:
      return XK_bracketleft;
    case 0xDC:
      return XK_backslash;
    case 0xDD:
      return XK_bracketright;
    case 0xDE:
      return XK_apostrophe;
  }

  return modcode;
}

void keyboard(input_t &input, uint16_t modcode, bool release) {
  auto &keyboard = ((input_raw_t*)input.get())->keyboard;
  KeyCode kc = XKeysymToKeycode(keyboard.get(), keysym(modcode));

  if(!kc) {
    return;
  }

  XTestFakeKeyEvent(keyboard.get(), kc, !release, 0);

  XSync(keyboard.get(), 0);
  XFlush(keyboard.get());
}

int alloc_gamepad(input_t &input, int nr) {
  return ((input_raw_t*)input.get())->alloc_gamepad(nr);
}

void free_gamepad(input_t &input, int nr) {
  ((input_raw_t*)input.get())->clear_gamepad(nr);
}

void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
  TUPLE_2D_REF(uinput, gamepad_state_old, ((input_raw_t*)input.get())->gamepads[nr]);


  auto bf = gamepad_state.buttonFlags ^ gamepad_state_old.buttonFlags;
  auto bf_new = gamepad_state.buttonFlags;

  if(bf) {
    // up pressed == -1, down pressed == 1, else 0
    if((DPAD_UP | DPAD_DOWN) & bf) {
      int button_state = bf_new & DPAD_UP ? -1 : (bf_new & DPAD_DOWN ? 1 : 0);

      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_HAT0Y, button_state);
    }

    if((DPAD_LEFT | DPAD_RIGHT) & bf) {
      int button_state = bf_new & DPAD_LEFT ? -1 : (bf_new & DPAD_RIGHT ? 1 : 0);

      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_HAT0X, button_state);
    }

    if(START & bf)        libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_START,  bf_new & START        ? 1 : 0);
    if(BACK & bf)         libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_SELECT, bf_new & BACK         ? 1 : 0);
    if(LEFT_STICK & bf)   libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_THUMBL, bf_new & LEFT_STICK   ? 1 : 0);
    if(RIGHT_STICK & bf)  libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_THUMBR, bf_new & RIGHT_STICK  ? 1 : 0);
    if(LEFT_BUTTON & bf)  libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_TL,     bf_new & LEFT_BUTTON  ? 1 : 0);
    if(RIGHT_BUTTON & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_TR,     bf_new & RIGHT_BUTTON ? 1 : 0);
    if(HOME & bf)         libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_MODE,   bf_new & HOME         ? 1 : 0);
    if(A & bf)            libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_SOUTH,  bf_new & A            ? 1 : 0);
    if(B & bf)            libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_EAST,   bf_new & B            ? 1 : 0);
    if(X & bf)            libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_NORTH,  bf_new & X            ? 1 : 0);
    if(Y & bf)            libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_WEST,   bf_new & Y            ? 1 : 0);
  }

  if(gamepad_state_old.lt != gamepad_state.lt) {
    libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_Z, gamepad_state.lt);
  }

  if(gamepad_state_old.rt != gamepad_state.rt) {
    libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_RZ, gamepad_state.rt);
  }

  if(gamepad_state_old.lsX != gamepad_state.lsX) {
    libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_X, gamepad_state.lsX);
  }

  if(gamepad_state_old.lsY != gamepad_state.lsY) {
    libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_Y, -gamepad_state.lsY);
  }

  if(gamepad_state_old.rsX != gamepad_state.rsX) {
    libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_RX, gamepad_state.rsX);
  }

  if(gamepad_state_old.rsY != gamepad_state.rsY) {
    libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_RY, -gamepad_state.rsY);
  }

  gamepad_state_old = gamepad_state;
  libevdev_uinput_write_event(uinput.get(), EV_SYN, SYN_REPORT, 0);
}

evdev_t mouse() {
  evdev_t dev  { libevdev_new() };

  libevdev_set_uniq(dev.get(), "Sunshine Mouse");
  libevdev_set_id_product(dev.get(), 0x4038);
  libevdev_set_id_vendor(dev.get(), 0x46D);
  libevdev_set_id_bustype(dev.get(), 0x3);
  libevdev_set_id_version(dev.get(), 0x111);
  libevdev_set_name(dev.get(), "Logitech Wireless Mouse PID:4038");

  libevdev_enable_event_type(dev.get(), EV_KEY);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_LEFT, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_RIGHT, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_MIDDLE, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_SIDE, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_EXTRA, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_FORWARD, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_BACK, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_TASK, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 280, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 281, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 282, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 283, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 284, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 285, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 286, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, 287, nullptr);

  libevdev_enable_event_type(dev.get(), EV_REL);
  libevdev_enable_event_code(dev.get(), EV_REL, REL_X, nullptr);
  libevdev_enable_event_code(dev.get(), EV_REL, REL_Y, nullptr);
  libevdev_enable_event_code(dev.get(), EV_REL, REL_WHEEL, nullptr);
  libevdev_enable_event_code(dev.get(), EV_REL, REL_WHEEL_HI_RES, nullptr);
  libevdev_enable_event_code(dev.get(), EV_REL, REL_HWHEEL, nullptr);
  libevdev_enable_event_code(dev.get(), EV_REL, REL_HWHEEL_HI_RES, nullptr);

  libevdev_enable_event_type(dev.get(), EV_MSC);
  libevdev_enable_event_code(dev.get(), EV_MSC, MSC_SCAN, nullptr);

  return dev;
}

evdev_t x360() {
  evdev_t dev { libevdev_new() };

  input_absinfo stick {
    0,
    -32768, 32767,
    16,
    128,
    0
  };

  input_absinfo trigger {
    0,
    0, 255,
    0,
    0,
    0
  };

  input_absinfo dpad {
    0,
    -1, 1,
    0,
    0,
    0
  };

  libevdev_set_uniq(dev.get(), "Sunshine Gamepad");
  libevdev_set_id_product(dev.get(), 0x28E);
  libevdev_set_id_vendor(dev.get(), 0x45E);
  libevdev_set_id_bustype(dev.get(), 0x3);
  libevdev_set_id_version(dev.get(), 0x110);
  libevdev_set_name(dev.get(), "Microsoft X-Box 360 pad");

  libevdev_enable_event_type(dev.get(), EV_KEY);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_WEST, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_EAST, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_NORTH, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_SOUTH, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_THUMBL, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_THUMBR, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_TR, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_TL, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_SELECT, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_MODE, nullptr);
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_START, nullptr);

  libevdev_enable_event_type(dev.get(), EV_ABS);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_HAT0Y, &dpad);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_HAT0X, &dpad);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_Z, &trigger);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_RZ, &trigger);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_X, &stick);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_RX, &stick);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_Y, &stick);
  libevdev_enable_event_code(dev.get(), EV_ABS, ABS_RY, &stick);

  return dev;
}

input_t input() {
  input_t result { new input_raw_t() };
  auto &gp = *(input_raw_t*)result.get();

  gp.keyboard.reset(XOpenDisplay(nullptr));

  // If we do not have a keyboard, gamepad or mouse, no input is possible and we should abort
  if(!gp.keyboard) {
    BOOST_LOG(fatal) << "Could not open x11 display for keyboard"sv;
    log_flush();
    std::abort();
  }

  gp.gamepads.resize(MAX_GAMEPADS);

  // Ensure starting from clean slate
  gp.clear();
  gp.mouse_dev = mouse();
  gp.gamepad_dev = x360();

  if(gp.create_mouse()) {
    log_flush();
    std::abort();
  }

  return result;
}

void freeInput(void *p) {
  auto *input = (input_raw_t*)p;
  delete input;
}

std::unique_ptr<deinit_t> init() { return nullptr; }
}
