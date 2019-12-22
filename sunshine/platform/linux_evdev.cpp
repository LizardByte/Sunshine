#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>

#include <iostream>
#include <string.h>

#include "common.h"
#include "sunshine/utility.h"

namespace platf {
using namespace std::literals;
using evdev_t = util::safe_ptr<libevdev, libevdev_free>;
using uinput_t = util::safe_ptr<libevdev_uinput, libevdev_uinput_destroy>;

struct input_raw_t {
  evdev_t dev;
  uinput_t uinput;
  display_t display;

  gamepad_state_t gamepad_state;
};

//TODO: Use libevdev for keyboard and mouse, then any  mention of X11 can be removed from linux_input.cpp
struct display_attr_t {
  display_attr_t() : display { XOpenDisplay(nullptr) }, window { DefaultRootWindow(display) }, attr {} {
    XGetWindowAttributes(display, window, &attr);
  }

  ~display_attr_t() {
    XCloseDisplay(display);
  }

  Display *display;
  Window window;
  XWindowAttributes attr;
};

void move_mouse(input_t &input, int deltaX, int deltaY) {
  auto &disp = *((display_attr_t *) ((input_raw_t*)input.get())->display.get());

  XWarpPointer(disp.display, None, None, 0, 0, 0, 0, deltaX, deltaY);
  XFlush(disp.display);
}

void button_mouse(input_t &input, int button, bool release) {
  auto &disp = *((display_attr_t *) ((input_raw_t*)input.get())->display.get());

  XTestFakeButtonEvent(disp.display, button, !release, CurrentTime);

  XFlush(disp.display);
}

void scroll(input_t &input, int distance) {
  auto &disp = *((display_attr_t *) ((input_raw_t*)input.get())->display.get());

  int button = distance > 0 ? 4 : 5;

  distance = std::abs(distance / 120);
  while(distance > 0) {
    --distance;

    XTestFakeButtonEvent(disp.display, button, True, CurrentTime);
    XTestFakeButtonEvent(disp.display, button, False, CurrentTime);

    XSync(disp.display, 0);
  }

  XFlush(disp.display);
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
      return XK_Print; //FIXME: is this correct? (printscreen)
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
      return XK_KP_Decimal; //FIXME: is this correct? (Comma)
    case 0x6D:
      return XK_KP_Subtract;
    case 0x6E:
      return XK_KP_Separator; //FIXME: is this correct? (Period)
    case 0x6F:
      return XK_KP_Divide;
    case 0x90:
      return XK_Num_Lock; //FIXME: is this correct: (NumlockClear)
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
  auto &disp = *((display_attr_t *) ((input_raw_t*)input.get())->display.get());
  KeyCode kc = XKeysymToKeycode(disp.display, keysym(modcode));

  if(!kc) {
    return;
  }

  XTestFakeKeyEvent(disp.display, kc, !release, 0);

  XSync(disp.display, 0);
  XFlush(disp.display);
}

namespace gp {
// up pressed == -1, down pressed == 1, else 0
void dpad_y(input_t &input, int button_state) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_HAT0Y, button_state);
}
// left pressed == -1, right pressed == 1, else 0
void dpad_x(input_t &input, int button_state) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_HAT0X, button_state);
}
void start(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_START, button_down);
}
void back(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_SELECT, button_down);
}
void left_stick(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_THUMBL, button_down);
}
void right_stick(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_THUMBR, button_down);
}
void left_button(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_TL, button_down);
}
void right_button(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_TR, button_down);
}
void home(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_MODE, button_down);
}
void a(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_SOUTH, button_down);
}
void b(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_EAST, button_down);
}
void x(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_NORTH, button_down);
}
void y(input_t &input, int button_down) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_WEST, button_down);
}
void left_trigger(input_t &input, std::uint8_t abs_z) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_Z, abs_z);
}
void right_trigger(input_t &input, std::uint8_t abs_z) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_RZ, abs_z);
}
void left_stick_x(input_t &input, std::int16_t x) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_X, x);
}
void left_stick_y(input_t &input, std::int16_t y) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_Y, -y);
}
void right_stick_x(input_t &input, std::int16_t x) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_RX, x);
}
void right_stick_y(input_t &input, std::int16_t y) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_RY, -y);
}
void sync(input_t &input) {
  auto &gp = *(input_raw_t*)input.get();
  libevdev_uinput_write_event(gp.uinput.get(), EV_SYN, SYN_REPORT, 0);
}
}

input_t input() {
  input_t result { new input_raw_t() };
  auto &gp = *(input_raw_t*)result.get();

  gp.dev.reset(libevdev_new());

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

  libevdev_set_uniq(gp.dev.get(), "Sunshine Gamepad");
  libevdev_set_id_product(gp.dev.get(), 0x28E); 
  libevdev_set_id_vendor(gp.dev.get(), 0x45E); 
  libevdev_set_id_bustype(gp.dev.get(), 0x3);
  libevdev_set_id_version(gp.dev.get(), 0x110);
  libevdev_set_name(gp.dev.get(), "Microsoft X-Box 360 pad");

  libevdev_enable_event_type(gp.dev.get(), EV_KEY);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_WEST, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_EAST, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_NORTH, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_SOUTH, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_THUMBL, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_THUMBR, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_TR, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_TL, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_SELECT, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_MODE, nullptr);
  libevdev_enable_event_code(gp.dev.get(), EV_KEY, BTN_START, nullptr);

  libevdev_enable_event_type(gp.dev.get(), EV_ABS);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_HAT0Y, &dpad);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_HAT0X, &dpad);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_Z, &trigger);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_RZ, &trigger);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_X, &stick);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_RX, &stick);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_Y, &stick);
  libevdev_enable_event_code(gp.dev.get(), EV_ABS, ABS_RY, &stick);

  libevdev_uinput *buf;
  int err = libevdev_uinput_create_from_device(gp.dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &buf);

  gp.uinput.reset(buf);
  if(err) {
    std::cout << "Could not create Sunshine Gamepad: "sv << strerror(-err) << std::endl;
    return nullptr;
  }

  gp.display = display();
  return result;
}

void freeInput(void *p) {
  auto *input = (input_raw_t*)p;
  delete input;
}
}
