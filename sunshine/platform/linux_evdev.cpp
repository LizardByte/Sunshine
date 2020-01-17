#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <cstring>
#include <filesystem>

#include "common.h"
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
  evdev_t gamepad_dev;
  uinput_t gamepad_input;

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
  else {
    btn_type = BTN_RIGHT;
    scan = 90002;
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
      return XK_KP_Decimal;
    case 0x6D:
      return XK_KP_Subtract;
    case 0x6E:
      return XK_KP_Separator;
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
    case 0x01: //FIXME: Moonlight doesn't support Super key
      return XK_Super_L;
    case 0x02:
      return XK_Super_R;
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

namespace gp {
// up pressed == -1, down pressed == 1, else 0
void dpad_y(input_t &input, int button_state) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_HAT0Y, button_state);
}
// left pressed == -1, right pressed == 1, else 0
void dpad_x(input_t &input, int button_state) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_HAT0X, button_state);
}
void start(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_START, button_down);
}
void back(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_SELECT, button_down);
}
void left_stick(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_THUMBL, button_down);
}
void right_stick(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_THUMBR, button_down);
}
void left_button(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_TL, button_down);
}
void right_button(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_TR, button_down);
}
void home(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_MODE, button_down);
}
void a(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_SOUTH, button_down);
}
void b(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_EAST, button_down);
}
void x(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_NORTH, button_down);
}
void y(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_KEY, BTN_WEST, button_down);
}
void left_trigger(input_t &input, std::uint8_t abs_z) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_Z, abs_z);
}
void right_trigger(input_t &input, std::uint8_t abs_z) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_RZ, abs_z);
}
void left_stick_x(input_t &input, std::int16_t x) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_X, x);
}
void left_stick_y(input_t &input, std::int16_t y) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_Y, -y);
}
void right_stick_x(input_t &input, std::int16_t x) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_RX, x);
}
void right_stick_y(input_t &input, std::int16_t y) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_ABS, ABS_RY, -y);
}
void sync(input_t &input) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.gamepad_input.get(), EV_SYN, SYN_REPORT, 0);
}
}

int mouse(input_raw_t &gp) {
  gp.mouse_dev.reset(libevdev_new());

  libevdev_set_uniq(gp.mouse_dev.get(), "Sunshine Gamepad");
  libevdev_set_id_product(gp.mouse_dev.get(), 0x4038);
  libevdev_set_id_vendor(gp.mouse_dev.get(), 0x46D);
  libevdev_set_id_bustype(gp.mouse_dev.get(), 0x3);
  libevdev_set_id_version(gp.mouse_dev.get(), 0x111);
  libevdev_set_name(gp.mouse_dev.get(), "Logitech Wireless Mouse PID:4038");

  libevdev_enable_event_type(gp.mouse_dev.get(), EV_KEY);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_LEFT, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_RIGHT, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_MIDDLE, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_SIDE, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_EXTRA, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_FORWARD, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_BACK, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, BTN_TASK, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 280, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 281, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 282, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 283, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 284, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 285, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 286, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_KEY, 287, nullptr);

  libevdev_enable_event_type(gp.mouse_dev.get(), EV_REL);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_REL, REL_X, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_REL, REL_Y, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_REL, REL_WHEEL, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_REL, REL_WHEEL_HI_RES, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_REL, REL_HWHEEL, nullptr);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_REL, REL_HWHEEL_HI_RES, nullptr);

  libevdev_enable_event_type(gp.mouse_dev.get(), EV_MSC);
  libevdev_enable_event_code(gp.mouse_dev.get(), EV_MSC, MSC_SCAN, nullptr);

  libevdev_uinput *buf;
  int err = libevdev_uinput_create_from_device(gp.mouse_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &buf);

  gp.mouse_input.reset(buf);
  if(err) {
    BOOST_LOG(error) << "Could not create Sunshine Mouse: "sv << strerror(-err);
    return -1;
  }

  return 0;
}

int gamepad(input_raw_t &gp) {
  gp.gamepad_dev.reset(libevdev_new());

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

  libevdev_set_uniq(gp.gamepad_dev.get(), "Sunshine Gamepad");
  libevdev_set_id_product(gp.gamepad_dev.get(), 0x28E);
  libevdev_set_id_vendor(gp.gamepad_dev.get(), 0x45E);
  libevdev_set_id_bustype(gp.gamepad_dev.get(), 0x3);
  libevdev_set_id_version(gp.gamepad_dev.get(), 0x110);
  libevdev_set_name(gp.gamepad_dev.get(), "Microsoft X-Box 360 pad");

  libevdev_enable_event_type(gp.gamepad_dev.get(), EV_KEY);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_WEST, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_EAST, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_NORTH, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_SOUTH, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_THUMBL, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_THUMBR, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_TR, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_TL, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_SELECT, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_MODE, nullptr);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_KEY, BTN_START, nullptr);

  libevdev_enable_event_type(gp.gamepad_dev.get(), EV_ABS);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_HAT0Y, &dpad);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_HAT0X, &dpad);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_Z, &trigger);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_RZ, &trigger);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_X, &stick);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_RX, &stick);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_Y, &stick);
  libevdev_enable_event_code(gp.gamepad_dev.get(), EV_ABS, ABS_RY, &stick);

  libevdev_uinput *buf;
  int err = libevdev_uinput_create_from_device(gp.gamepad_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &buf);

  gp.gamepad_input.reset(buf);
  if(err) {
    BOOST_LOG(error) << "Could not create Sunshine Gamepad: "sv << strerror(-err);
    return -1;
  }

  return 0;
}

input_t input() {
  input_t result { new input_raw_t() };
  auto &gp = *(input_raw_t*)result.get();

  gp.keyboard.reset(XOpenDisplay(nullptr));
  if(!gp.keyboard) {
    return nullptr;
  }

  if(gamepad(gp)) {
    return nullptr;
  }

  if(mouse(gp)) {
    return nullptr;
  }

  std::filesystem::path mouse_path { "sunshine_mouse" };
  std::filesystem::path gamepad_path { "sunshine_gamepad" };
  if(std::filesystem::is_symlink(mouse_path)) {
    std::filesystem::remove(mouse_path);
  }
  if(std::filesystem::is_symlink(gamepad_path)) {
    std::filesystem::remove(gamepad_path);
  }

  std::filesystem::create_symlink(libevdev_uinput_get_devnode(gp.mouse_input.get()), mouse_path);
  std::filesystem::create_symlink(libevdev_uinput_get_devnode(gp.gamepad_input.get()), gamepad_path);

  return result;
}

void freeInput(void *p) {
  auto *input = (input_raw_t*)p;
  delete input;
}
}
