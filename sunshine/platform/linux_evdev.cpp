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
constexpr std::uint16_t DPAD_UP      = 0x0001;
constexpr std::uint16_t DPAD_DOWN    = 0x0002;
constexpr std::uint16_t DPAD_LEFT    = 0x0004;
constexpr std::uint16_t DPAD_RIGHT   = 0x0008;
constexpr std::uint16_t START        = 0x0010;
constexpr std::uint16_t BACK         = 0x0020;
constexpr std::uint16_t LEFT_STICK   = 0x0040;
constexpr std::uint16_t RIGHT_STICK  = 0x0080;
constexpr std::uint16_t LEFT_BUTTON  = 0x0100;
constexpr std::uint16_t RIGHT_BUTTON = 0x0200;
constexpr std::uint16_t HOME         = 0x0400;
constexpr std::uint16_t A            = 0x1000;
constexpr std::uint16_t B            = 0x2000;
constexpr std::uint16_t X            = 0x4000;
constexpr std::uint16_t Y            = 0x8000;
	
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

void gamepad(input_t &input, const gamepad_state_t &gamepad_state) {
  auto &gp = *(input_raw_t*)input.get();

  auto bf = gamepad_state.buttonFlags ^ gp.gamepad_state.buttonFlags;
  auto bf_new = gamepad_state.buttonFlags;

  if(bf) {
    // up pressed == -1, down pressed == 1, else 0
    if(DPAD_UP & bf) {
      int val = bf_new & DPAD_UP ? -1 : (bf_new & DPAD_DOWN ? 1 : 0);
      libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_HAT0Y, val);
    }
    else if(DPAD_DOWN & bf) {
      int val = bf_new & DPAD_DOWN ? 1 : 0;
      libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_HAT0Y, val);
    }

    if(DPAD_LEFT & bf) {
      int val = bf_new & DPAD_LEFT ? -1 : (bf_new & DPAD_RIGHT ? 1 : 0);
      libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_HAT0X, val);
    }
    else if(DPAD_RIGHT & bf) {
      int val = bf_new & DPAD_RIGHT ? 1 : 0;
      libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_HAT0X, val);
    }

    if(START & bf)        libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_START, bf_new & START        ? 1 : 0);
    if(BACK & bf)         libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_SELECT, bf_new & BACK        ? 1 : 0);
    if(LEFT_STICK & bf)   libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_THUMBL, bf_new & LEFT_STICK  ? 1 : 0);
    if(RIGHT_STICK & bf)  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_THUMBR, bf_new & RIGHT_STICK ? 1 : 0);
    if(LEFT_BUTTON & bf)  libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_TL, bf_new & LEFT_BUTTON     ? 1 : 0);
    if(RIGHT_BUTTON & bf) libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_TR, bf_new & RIGHT_BUTTON    ? 1 : 0);
    if(HOME & bf)         libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_MODE, bf_new & HOME          ? 1 : 0);
    if(A & bf)            libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_SOUTH, bf_new & A            ? 1 : 0);
    if(B & bf)            libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_EAST, bf_new & B             ? 1 : 0);
    if(X & bf)            libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_NORTH, bf_new & X            ? 1 : 0);
    if(Y & bf)            libevdev_uinput_write_event(gp.uinput.get(), EV_KEY, BTN_WEST, bf_new & Y             ? 1 : 0);
  }

  if(gp.gamepad_state.lt != gamepad_state.lt) {
    libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_Z, gamepad_state.lt);
  }

  if(gp.gamepad_state.rt != gamepad_state.rt) {
    libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_RZ, gamepad_state.rt);
  }

  if(gp.gamepad_state.lsX != gamepad_state.lsX) {
    libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_X, gamepad_state.lsX);
  }

  if(gp.gamepad_state.lsY != gamepad_state.lsY) {
    libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_Y, -gamepad_state.lsY);
  }

  if(gp.gamepad_state.rsX != gamepad_state.rsX) {
    libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_RX, gamepad_state.rsX);
  }

  if(gp.gamepad_state.rsY != gamepad_state.rsY) {
    libevdev_uinput_write_event(gp.uinput.get(), EV_ABS, ABS_RY, -gamepad_state.rsY);
  }

  gp.gamepad_state = gamepad_state;
  libevdev_uinput_write_event(gp.uinput.get(), EV_SYN, SYN_REPORT, 0);
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
