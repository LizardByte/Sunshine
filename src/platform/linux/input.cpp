/**
 * @file src/platform/linux/input.cpp
 * @brief todo
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#ifdef SUNSHINE_BUILD_X11
  #include <X11/Xutil.h>
  #include <X11/extensions/XTest.h>
  #include <X11/keysym.h>
  #include <X11/keysymdef.h>
#endif

#include <boost/locale.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "src/main.h"
#include "src/platform/common.h"
#include "src/utility.h"

#include "src/platform/common.h"

#include "misc.h"

// Support older versions
#ifndef REL_HWHEEL_HI_RES
  #define REL_HWHEEL_HI_RES 0x0c
#endif

#ifndef REL_WHEEL_HI_RES
  #define REL_WHEEL_HI_RES 0x0b
#endif

using namespace std::literals;

namespace platf {
#ifdef SUNSHINE_BUILD_X11
  namespace x11 {
  #define _FN(x, ret, args)    \
    typedef ret(*x##_fn) args; \
    static x##_fn x

    _FN(OpenDisplay, Display *, (_Xconst char *display_name));
    _FN(CloseDisplay, int, (Display * display));
    _FN(InitThreads, Status, (void) );
    _FN(Flush, int, (Display *) );

    namespace tst {
      _FN(FakeMotionEvent, int, (Display * dpy, int screen_numer, int x, int y, unsigned long delay));
      _FN(FakeRelativeMotionEvent, int, (Display * dpy, int deltaX, int deltaY, unsigned long delay));
      _FN(FakeButtonEvent, int, (Display * dpy, unsigned int button, Bool is_press, unsigned long delay));
      _FN(FakeKeyEvent, int, (Display * dpy, unsigned int keycode, Bool is_press, unsigned long delay));

      static int
      init() {
        static void *handle { nullptr };
        static bool funcs_loaded = false;

        if (funcs_loaded) return 0;

        if (!handle) {
          handle = dyn::handle({ "libXtst.so.6", "libXtst.so" });
          if (!handle) {
            return -1;
          }
        }

        std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
          { (dyn::apiproc *) &FakeMotionEvent, "XTestFakeMotionEvent" },
          { (dyn::apiproc *) &FakeRelativeMotionEvent, "XTestFakeRelativeMotionEvent" },
          { (dyn::apiproc *) &FakeButtonEvent, "XTestFakeButtonEvent" },
          { (dyn::apiproc *) &FakeKeyEvent, "XTestFakeKeyEvent" },
        };

        if (dyn::load(handle, funcs)) {
          return -1;
        }

        funcs_loaded = true;
        return 0;
      }
    }  // namespace tst

    static int
    init() {
      static void *handle { nullptr };
      static bool funcs_loaded = false;

      if (funcs_loaded) return 0;

      if (!handle) {
        handle = dyn::handle({ "libX11.so.6", "libX11.so" });
        if (!handle) {
          return -1;
        }
      }

      std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
        { (dyn::apiproc *) &OpenDisplay, "XOpenDisplay" },
        { (dyn::apiproc *) &CloseDisplay, "XCloseDisplay" },
        { (dyn::apiproc *) &InitThreads, "XInitThreads" },
        { (dyn::apiproc *) &Flush, "XFlush" },
      };

      if (dyn::load(handle, funcs)) {
        return -1;
      }

      funcs_loaded = true;
      return 0;
    }
  }  // namespace x11
#endif

  constexpr auto mail_evdev = "platf::evdev"sv;

  using evdev_t = util::safe_ptr<libevdev, libevdev_free>;
  using uinput_t = util::safe_ptr<libevdev_uinput, libevdev_uinput_destroy>;

  constexpr pollfd read_pollfd { -1, 0, 0 };
  KITTY_USING_MOVE_T(pollfd_t, pollfd, read_pollfd, {
    if (el.fd >= 0) {
      ioctl(el.fd, EVIOCGRAB, (void *) 0);

      close(el.fd);
    }
  });

  using mail_evdev_t = std::tuple<int, uinput_t::pointer, feedback_queue_t, pollfd_t>;

  struct keycode_t {
    std::uint32_t keycode;
    std::uint32_t scancode;

#ifdef SUNSHINE_BUILD_X11
    KeySym keysym;
#endif
  };

  constexpr auto UNKNOWN = 0;

  /**
   * @brief Initializes the keycode constants for translating
   *        moonlight keycodes to linux/X11 keycodes.
   */
  static constexpr std::array<keycode_t, 0xE3>
  init_keycodes() {
    std::array<keycode_t, 0xE3> keycodes {};

#ifdef SUNSHINE_BUILD_X11
  #define __CONVERT_UNSAFE(wincode, linuxcode, scancode, keysym) \
    keycodes[wincode] = keycode_t { linuxcode, scancode, keysym };
#else
  #define __CONVERT_UNSAFE(wincode, linuxcode, scancode, keysym) \
    keycodes[wincode] = keycode_t { linuxcode, scancode };
#endif

#define __CONVERT(wincode, linuxcode, scancode, keysym)                               \
  static_assert(wincode < keycodes.size(), "Keycode doesn't fit into keycode array"); \
  static_assert(wincode >= 0, "Are you mad?, keycode needs to be greater than zero"); \
  __CONVERT_UNSAFE(wincode, linuxcode, scancode, keysym)

    __CONVERT(0x08 /* VKEY_BACK */, KEY_BACKSPACE, 0x7002A, XK_BackSpace);
    __CONVERT(0x09 /* VKEY_TAB */, KEY_TAB, 0x7002B, XK_Tab);
    __CONVERT(0x0C /* VKEY_CLEAR */, KEY_CLEAR, UNKNOWN, XK_Clear);
    __CONVERT(0x0D /* VKEY_RETURN */, KEY_ENTER, 0x70028, XK_Return);
    __CONVERT(0x10 /* VKEY_SHIFT */, KEY_LEFTSHIFT, 0x700E1, XK_Shift_L);
    __CONVERT(0x11 /* VKEY_CONTROL */, KEY_LEFTCTRL, 0x700E0, XK_Control_L);
    __CONVERT(0x12 /* VKEY_MENU */, KEY_LEFTALT, UNKNOWN, XK_Alt_L);
    __CONVERT(0x13 /* VKEY_PAUSE */, KEY_PAUSE, UNKNOWN, XK_Pause);
    __CONVERT(0x14 /* VKEY_CAPITAL */, KEY_CAPSLOCK, 0x70039, XK_Caps_Lock);
    __CONVERT(0x15 /* VKEY_KANA */, KEY_KATAKANAHIRAGANA, UNKNOWN, XK_Kana_Shift);
    __CONVERT(0x16 /* VKEY_HANGUL */, KEY_HANGEUL, UNKNOWN, XK_Hangul);
    __CONVERT(0x17 /* VKEY_JUNJA */, KEY_HANJA, UNKNOWN, XK_Hangul_Jeonja);
    __CONVERT(0x19 /* VKEY_KANJI */, KEY_KATAKANA, UNKNOWN, XK_Kanji);
    __CONVERT(0x1B /* VKEY_ESCAPE */, KEY_ESC, 0x70029, XK_Escape);
    __CONVERT(0x20 /* VKEY_SPACE */, KEY_SPACE, 0x7002C, XK_space);
    __CONVERT(0x21 /* VKEY_PRIOR */, KEY_PAGEUP, 0x7004B, XK_Page_Up);
    __CONVERT(0x22 /* VKEY_NEXT */, KEY_PAGEDOWN, 0x7004E, XK_Page_Down);
    __CONVERT(0x23 /* VKEY_END */, KEY_END, 0x7004D, XK_End);
    __CONVERT(0x24 /* VKEY_HOME */, KEY_HOME, 0x7004A, XK_Home);
    __CONVERT(0x25 /* VKEY_LEFT */, KEY_LEFT, 0x70050, XK_Left);
    __CONVERT(0x26 /* VKEY_UP */, KEY_UP, 0x70052, XK_Up);
    __CONVERT(0x27 /* VKEY_RIGHT */, KEY_RIGHT, 0x7004F, XK_Right);
    __CONVERT(0x28 /* VKEY_DOWN */, KEY_DOWN, 0x70051, XK_Down);
    __CONVERT(0x29 /* VKEY_SELECT */, KEY_SELECT, UNKNOWN, XK_Select);
    __CONVERT(0x2A /* VKEY_PRINT */, KEY_PRINT, UNKNOWN, XK_Print);
    __CONVERT(0x2C /* VKEY_SNAPSHOT */, KEY_SYSRQ, 0x70046, XK_Sys_Req);
    __CONVERT(0x2D /* VKEY_INSERT */, KEY_INSERT, 0x70049, XK_Insert);
    __CONVERT(0x2E /* VKEY_DELETE */, KEY_DELETE, 0x7004C, XK_Delete);
    __CONVERT(0x2F /* VKEY_HELP */, KEY_HELP, UNKNOWN, XK_Help);
    __CONVERT(0x30 /* VKEY_0 */, KEY_0, 0x70027, XK_0);
    __CONVERT(0x31 /* VKEY_1 */, KEY_1, 0x7001E, XK_1);
    __CONVERT(0x32 /* VKEY_2 */, KEY_2, 0x7001F, XK_2);
    __CONVERT(0x33 /* VKEY_3 */, KEY_3, 0x70020, XK_3);
    __CONVERT(0x34 /* VKEY_4 */, KEY_4, 0x70021, XK_4);
    __CONVERT(0x35 /* VKEY_5 */, KEY_5, 0x70022, XK_5);
    __CONVERT(0x36 /* VKEY_6 */, KEY_6, 0x70023, XK_6);
    __CONVERT(0x37 /* VKEY_7 */, KEY_7, 0x70024, XK_7);
    __CONVERT(0x38 /* VKEY_8 */, KEY_8, 0x70025, XK_8);
    __CONVERT(0x39 /* VKEY_9 */, KEY_9, 0x70026, XK_9);
    __CONVERT(0x41 /* VKEY_A */, KEY_A, 0x70004, XK_A);
    __CONVERT(0x42 /* VKEY_B */, KEY_B, 0x70005, XK_B);
    __CONVERT(0x43 /* VKEY_C */, KEY_C, 0x70006, XK_C);
    __CONVERT(0x44 /* VKEY_D */, KEY_D, 0x70007, XK_D);
    __CONVERT(0x45 /* VKEY_E */, KEY_E, 0x70008, XK_E);
    __CONVERT(0x46 /* VKEY_F */, KEY_F, 0x70009, XK_F);
    __CONVERT(0x47 /* VKEY_G */, KEY_G, 0x7000A, XK_G);
    __CONVERT(0x48 /* VKEY_H */, KEY_H, 0x7000B, XK_H);
    __CONVERT(0x49 /* VKEY_I */, KEY_I, 0x7000C, XK_I);
    __CONVERT(0x4A /* VKEY_J */, KEY_J, 0x7000D, XK_J);
    __CONVERT(0x4B /* VKEY_K */, KEY_K, 0x7000E, XK_K);
    __CONVERT(0x4C /* VKEY_L */, KEY_L, 0x7000F, XK_L);
    __CONVERT(0x4D /* VKEY_M */, KEY_M, 0x70010, XK_M);
    __CONVERT(0x4E /* VKEY_N */, KEY_N, 0x70011, XK_N);
    __CONVERT(0x4F /* VKEY_O */, KEY_O, 0x70012, XK_O);
    __CONVERT(0x50 /* VKEY_P */, KEY_P, 0x70013, XK_P);
    __CONVERT(0x51 /* VKEY_Q */, KEY_Q, 0x70014, XK_Q);
    __CONVERT(0x52 /* VKEY_R */, KEY_R, 0x70015, XK_R);
    __CONVERT(0x53 /* VKEY_S */, KEY_S, 0x70016, XK_S);
    __CONVERT(0x54 /* VKEY_T */, KEY_T, 0x70017, XK_T);
    __CONVERT(0x55 /* VKEY_U */, KEY_U, 0x70018, XK_U);
    __CONVERT(0x56 /* VKEY_V */, KEY_V, 0x70019, XK_V);
    __CONVERT(0x57 /* VKEY_W */, KEY_W, 0x7001A, XK_W);
    __CONVERT(0x58 /* VKEY_X */, KEY_X, 0x7001B, XK_X);
    __CONVERT(0x59 /* VKEY_Y */, KEY_Y, 0x7001C, XK_Y);
    __CONVERT(0x5A /* VKEY_Z */, KEY_Z, 0x7001D, XK_Z);
    __CONVERT(0x5B /* VKEY_LWIN */, KEY_LEFTMETA, 0x700E3, XK_Meta_L);
    __CONVERT(0x5C /* VKEY_RWIN */, KEY_RIGHTMETA, 0x700E7, XK_Meta_R);
    __CONVERT(0x5F /* VKEY_SLEEP */, KEY_SLEEP, UNKNOWN, UNKNOWN);
    __CONVERT(0x60 /* VKEY_NUMPAD0 */, KEY_KP0, 0x70062, XK_KP_0);
    __CONVERT(0x61 /* VKEY_NUMPAD1 */, KEY_KP1, 0x70059, XK_KP_1);
    __CONVERT(0x62 /* VKEY_NUMPAD2 */, KEY_KP2, 0x7005A, XK_KP_2);
    __CONVERT(0x63 /* VKEY_NUMPAD3 */, KEY_KP3, 0x7005B, XK_KP_3);
    __CONVERT(0x64 /* VKEY_NUMPAD4 */, KEY_KP4, 0x7005C, XK_KP_4);
    __CONVERT(0x65 /* VKEY_NUMPAD5 */, KEY_KP5, 0x7005D, XK_KP_5);
    __CONVERT(0x66 /* VKEY_NUMPAD6 */, KEY_KP6, 0x7005E, XK_KP_6);
    __CONVERT(0x67 /* VKEY_NUMPAD7 */, KEY_KP7, 0x7005F, XK_KP_7);
    __CONVERT(0x68 /* VKEY_NUMPAD8 */, KEY_KP8, 0x70060, XK_KP_8);
    __CONVERT(0x69 /* VKEY_NUMPAD9 */, KEY_KP9, 0x70061, XK_KP_9);
    __CONVERT(0x6A /* VKEY_MULTIPLY */, KEY_KPASTERISK, 0x70055, XK_KP_Multiply);
    __CONVERT(0x6B /* VKEY_ADD */, KEY_KPPLUS, 0x70057, XK_KP_Add);
    __CONVERT(0x6C /* VKEY_SEPARATOR */, KEY_KPCOMMA, UNKNOWN, XK_KP_Separator);
    __CONVERT(0x6D /* VKEY_SUBTRACT */, KEY_KPMINUS, 0x70056, XK_KP_Subtract);
    __CONVERT(0x6E /* VKEY_DECIMAL */, KEY_KPDOT, 0x70063, XK_KP_Decimal);
    __CONVERT(0x6F /* VKEY_DIVIDE */, KEY_KPSLASH, 0x70054, XK_KP_Divide);
    __CONVERT(0x70 /* VKEY_F1 */, KEY_F1, 0x70046, XK_F1);
    __CONVERT(0x71 /* VKEY_F2 */, KEY_F2, 0x70047, XK_F2);
    __CONVERT(0x72 /* VKEY_F3 */, KEY_F3, 0x70048, XK_F3);
    __CONVERT(0x73 /* VKEY_F4 */, KEY_F4, 0x70049, XK_F4);
    __CONVERT(0x74 /* VKEY_F5 */, KEY_F5, 0x7004a, XK_F5);
    __CONVERT(0x75 /* VKEY_F6 */, KEY_F6, 0x7004b, XK_F6);
    __CONVERT(0x76 /* VKEY_F7 */, KEY_F7, 0x7004c, XK_F7);
    __CONVERT(0x77 /* VKEY_F8 */, KEY_F8, 0x7004d, XK_F8);
    __CONVERT(0x78 /* VKEY_F9 */, KEY_F9, 0x7004e, XK_F9);
    __CONVERT(0x79 /* VKEY_F10 */, KEY_F10, 0x70044, XK_F10);
    __CONVERT(0x7A /* VKEY_F11 */, KEY_F11, 0x70044, XK_F11);
    __CONVERT(0x7B /* VKEY_F12 */, KEY_F12, 0x70045, XK_F12);
    __CONVERT(0x7C /* VKEY_F13 */, KEY_F13, 0x7003a, XK_F13);
    __CONVERT(0x7D /* VKEY_F14 */, KEY_F14, 0x7003b, XK_F14);
    __CONVERT(0x7E /* VKEY_F15 */, KEY_F15, 0x7003c, XK_F15);
    __CONVERT(0x7F /* VKEY_F16 */, KEY_F16, 0x7003d, XK_F16);
    __CONVERT(0x80 /* VKEY_F17 */, KEY_F17, 0x7003e, XK_F17);
    __CONVERT(0x81 /* VKEY_F18 */, KEY_F18, 0x7003f, XK_F18);
    __CONVERT(0x82 /* VKEY_F19 */, KEY_F19, 0x70040, XK_F19);
    __CONVERT(0x83 /* VKEY_F20 */, KEY_F20, 0x70041, XK_F20);
    __CONVERT(0x84 /* VKEY_F21 */, KEY_F21, 0x70042, XK_F21);
    __CONVERT(0x85 /* VKEY_F22 */, KEY_F12, 0x70043, XK_F12);
    __CONVERT(0x86 /* VKEY_F23 */, KEY_F23, 0x70044, XK_F23);
    __CONVERT(0x87 /* VKEY_F24 */, KEY_F24, 0x70045, XK_F24);
    __CONVERT(0x90 /* VKEY_NUMLOCK */, KEY_NUMLOCK, 0x70053, XK_Num_Lock);
    __CONVERT(0x91 /* VKEY_SCROLL */, KEY_SCROLLLOCK, 0x70047, XK_Scroll_Lock);
    __CONVERT(0xA0 /* VKEY_LSHIFT */, KEY_LEFTSHIFT, 0x700E1, XK_Shift_L);
    __CONVERT(0xA1 /* VKEY_RSHIFT */, KEY_RIGHTSHIFT, 0x700E5, XK_Shift_R);
    __CONVERT(0xA2 /* VKEY_LCONTROL */, KEY_LEFTCTRL, 0x700E0, XK_Control_L);
    __CONVERT(0xA3 /* VKEY_RCONTROL */, KEY_RIGHTCTRL, 0x700E4, XK_Control_R);
    __CONVERT(0xA4 /* VKEY_LMENU */, KEY_LEFTALT, 0x7002E, XK_Alt_L);
    __CONVERT(0xA5 /* VKEY_RMENU */, KEY_RIGHTALT, 0x700E6, XK_Alt_R);
    __CONVERT(0xBA /* VKEY_OEM_1 */, KEY_SEMICOLON, 0x70033, XK_semicolon);
    __CONVERT(0xBB /* VKEY_OEM_PLUS */, KEY_EQUAL, 0x7002E, XK_equal);
    __CONVERT(0xBC /* VKEY_OEM_COMMA */, KEY_COMMA, 0x70036, XK_comma);
    __CONVERT(0xBD /* VKEY_OEM_MINUS */, KEY_MINUS, 0x7002D, XK_minus);
    __CONVERT(0xBE /* VKEY_OEM_PERIOD */, KEY_DOT, 0x70037, XK_period);
    __CONVERT(0xBF /* VKEY_OEM_2 */, KEY_SLASH, 0x70038, XK_slash);
    __CONVERT(0xC0 /* VKEY_OEM_3 */, KEY_GRAVE, 0x70035, XK_grave);
    __CONVERT(0xDB /* VKEY_OEM_4 */, KEY_LEFTBRACE, 0x7002F, XK_braceleft);
    __CONVERT(0xDC /* VKEY_OEM_5 */, KEY_BACKSLASH, 0x70031, XK_backslash);
    __CONVERT(0xDD /* VKEY_OEM_6 */, KEY_RIGHTBRACE, 0x70030, XK_braceright);
    __CONVERT(0xDE /* VKEY_OEM_7 */, KEY_APOSTROPHE, 0x70034, XK_apostrophe);
    __CONVERT(0xE2 /* VKEY_NON_US_BACKSLASH */, KEY_102ND, 0x70064, XK_backslash);
#undef __CONVERT
#undef __CONVERT_UNSAFE

    return keycodes;
  }

  static constexpr auto keycodes = init_keycodes();

  constexpr touch_port_t target_touch_port {
    0, 0,
    19200, 12000
  };

  static std::pair<std::uint32_t, std::uint32_t>
  operator*(const std::pair<std::uint32_t, std::uint32_t> &l, int r) {
    return {
      l.first * r,
      l.second * r,
    };
  }

  static std::pair<std::uint32_t, std::uint32_t>
  operator/(const std::pair<std::uint32_t, std::uint32_t> &l, int r) {
    return {
      l.first / r,
      l.second / r,
    };
  }

  static std::pair<std::uint32_t, std::uint32_t> &
  operator+=(std::pair<std::uint32_t, std::uint32_t> &l, const std::pair<std::uint32_t, std::uint32_t> &r) {
    l.first += r.first;
    l.second += r.second;

    return l;
  }

  static inline void
  print(const ff_envelope &envelope) {
    BOOST_LOG(debug)
      << "Envelope:"sv << std::endl
      << "  attack_length: " << envelope.attack_length << std::endl
      << "  attack_level: " << envelope.attack_level << std::endl
      << "  fade_length: " << envelope.fade_length << std::endl
      << "  fade_level: " << envelope.fade_level;
  }

  static inline void
  print(const ff_replay &replay) {
    BOOST_LOG(debug)
      << "Replay:"sv << std::endl
      << "  length: "sv << replay.length << std::endl
      << "  delay: "sv << replay.delay;
  }

  static inline void
  print(const ff_trigger &trigger) {
    BOOST_LOG(debug)
      << "Trigger:"sv << std::endl
      << "  button: "sv << trigger.button << std::endl
      << "  interval: "sv << trigger.interval;
  }

  static inline void
  print(const ff_effect &effect) {
    BOOST_LOG(debug)
      << std::endl
      << std::endl
      << "Received rumble effect with id: ["sv << effect.id << ']';

    switch (effect.type) {
      case FF_CONSTANT:
        BOOST_LOG(debug)
          << "FF_CONSTANT:"sv << std::endl
          << "  direction: "sv << effect.direction << std::endl
          << "  level: "sv << effect.u.constant.level;

        print(effect.u.constant.envelope);
        break;

      case FF_PERIODIC:
        BOOST_LOG(debug)
          << "FF_CONSTANT:"sv << std::endl
          << "  direction: "sv << effect.direction << std::endl
          << "  waveform: "sv << effect.u.periodic.waveform << std::endl
          << "  period: "sv << effect.u.periodic.period << std::endl
          << "  magnitude: "sv << effect.u.periodic.magnitude << std::endl
          << "  offset: "sv << effect.u.periodic.offset << std::endl
          << "  phase: "sv << effect.u.periodic.phase;

        print(effect.u.periodic.envelope);
        break;

      case FF_RAMP:
        BOOST_LOG(debug)
          << "FF_RAMP:"sv << std::endl
          << "  direction: "sv << effect.direction << std::endl
          << "  start_level:" << effect.u.ramp.start_level << std::endl
          << "  end_level:" << effect.u.ramp.end_level;

        print(effect.u.ramp.envelope);
        break;

      case FF_RUMBLE:
        BOOST_LOG(debug)
          << "FF_RUMBLE:" << std::endl
          << "  direction: "sv << effect.direction << std::endl
          << "  strong_magnitude: " << effect.u.rumble.strong_magnitude << std::endl
          << "  weak_magnitude: " << effect.u.rumble.weak_magnitude;
        break;

      case FF_SPRING:
        BOOST_LOG(debug)
          << "FF_SPRING:" << std::endl
          << "  direction: "sv << effect.direction;
        break;

      case FF_FRICTION:
        BOOST_LOG(debug)
          << "FF_FRICTION:" << std::endl
          << "  direction: "sv << effect.direction;
        break;

      case FF_DAMPER:
        BOOST_LOG(debug)
          << "FF_DAMPER:" << std::endl
          << "  direction: "sv << effect.direction;
        break;

      case FF_INERTIA:
        BOOST_LOG(debug)
          << "FF_INERTIA:" << std::endl
          << "  direction: "sv << effect.direction;
        break;

      case FF_CUSTOM:
        BOOST_LOG(debug)
          << "FF_CUSTOM:" << std::endl
          << "  direction: "sv << effect.direction;
        break;

      default:
        BOOST_LOG(debug)
          << "FF_UNKNOWN:" << std::endl
          << "  direction: "sv << effect.direction;
        break;
    }

    print(effect.replay);
    print(effect.trigger);
  }

  // Emulate rumble effects
  class effect_t {
  public:
    KITTY_DEFAULT_CONSTR_MOVE(effect_t)

    effect_t(std::uint8_t gamepadnr, uinput_t::pointer dev, feedback_queue_t &&q):
        gamepadnr { gamepadnr }, dev { dev }, rumble_queue { std::move(q) }, gain { 0xFFFF }, id_to_data {} {}

    class data_t {
    public:
      KITTY_DEFAULT_CONSTR(data_t)

      data_t(const ff_effect &effect):
          delay { effect.replay.delay },
          length { effect.replay.length },
          end_point { std::chrono::steady_clock::time_point::min() },
          envelope {},
          start {},
          end {} {
        switch (effect.type) {
          case FF_CONSTANT:
            start.weak = effect.u.constant.level;
            start.strong = effect.u.constant.level;
            end.weak = effect.u.constant.level;
            end.strong = effect.u.constant.level;

            envelope = effect.u.constant.envelope;
            break;
          case FF_PERIODIC:
            start.weak = effect.u.periodic.magnitude;
            start.strong = effect.u.periodic.magnitude;
            end.weak = effect.u.periodic.magnitude;
            end.strong = effect.u.periodic.magnitude;

            envelope = effect.u.periodic.envelope;
            break;

          case FF_RAMP:
            start.weak = effect.u.ramp.start_level;
            start.strong = effect.u.ramp.start_level;
            end.weak = effect.u.ramp.end_level;
            end.strong = effect.u.ramp.end_level;

            envelope = effect.u.ramp.envelope;
            break;

          case FF_RUMBLE:
            start.weak = effect.u.rumble.weak_magnitude;
            start.strong = effect.u.rumble.strong_magnitude;
            end.weak = effect.u.rumble.weak_magnitude;
            end.strong = effect.u.rumble.strong_magnitude;
            break;

          default:
            BOOST_LOG(warning) << "Effect type ["sv << effect.id << "] not implemented"sv;
        }
      }

      std::uint32_t
      magnitude(std::chrono::milliseconds time_left, std::uint32_t start, std::uint32_t end) {
        auto rel = end - start;

        return start + (rel * time_left.count() / length.count());
      }

      std::pair<std::uint32_t, std::uint32_t>
      rumble(std::chrono::steady_clock::time_point tp) {
        if (end_point < tp) {
          return {};
        }

        auto time_left =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            end_point - tp);

        // If it needs to be delayed'
        if (time_left > length) {
          return {};
        }

        auto t = length - time_left;

        auto weak = magnitude(t, start.weak, end.weak);
        auto strong = magnitude(t, start.strong, end.strong);

        if (t.count() < envelope.attack_length) {
          weak = (envelope.attack_level * t.count() + weak * (envelope.attack_length - t.count())) / envelope.attack_length;
          strong = (envelope.attack_level * t.count() + strong * (envelope.attack_length - t.count())) / envelope.attack_length;
        }
        else if (time_left.count() < envelope.fade_length) {
          auto dt = (t - length).count() + envelope.fade_length;

          weak = (envelope.fade_level * dt + weak * (envelope.fade_length - dt)) / envelope.fade_length;
          strong = (envelope.fade_level * dt + strong * (envelope.fade_length - dt)) / envelope.fade_length;
        }

        return {
          weak, strong
        };
      }

      void
      activate() {
        end_point = std::chrono::steady_clock::now() + delay + length;
      }

      void
      deactivate() {
        end_point = std::chrono::steady_clock::time_point::min();
      }

      std::chrono::milliseconds delay;
      std::chrono::milliseconds length;

      std::chrono::steady_clock::time_point end_point;

      ff_envelope envelope;
      struct {
        std::uint32_t weak, strong;
      } start;

      struct {
        std::uint32_t weak, strong;
      } end;
    };

    std::pair<std::uint32_t, std::uint32_t>
    rumble(std::chrono::steady_clock::time_point tp) {
      std::pair<std::uint32_t, std::uint32_t> weak_strong {};
      for (auto &[_, data] : id_to_data) {
        weak_strong += data.rumble(tp);
      }

      std::clamp<std::uint32_t>(weak_strong.first, 0, 0xFFFF);
      std::clamp<std::uint32_t>(weak_strong.second, 0, 0xFFFF);

      old_rumble = weak_strong * gain / 0xFFFF;
      return old_rumble;
    }

    void
    upload(const ff_effect &effect) {
      print(effect);

      auto it = id_to_data.find(effect.id);

      if (it == std::end(id_to_data)) {
        id_to_data.emplace(effect.id, effect);
        return;
      }

      data_t data { effect };

      data.end_point = it->second.end_point;
      it->second = data;
    }

    void
    activate(int id) {
      auto it = id_to_data.find(id);

      if (it != std::end(id_to_data)) {
        it->second.activate();
      }
    }

    void
    deactivate(int id) {
      auto it = id_to_data.find(id);

      if (it != std::end(id_to_data)) {
        it->second.deactivate();
      }
    }

    void
    erase(int id) {
      id_to_data.erase(id);
      BOOST_LOG(debug) << "Removed rumble effect id ["sv << id << ']';
    }

    // Client-relative gamepad index for rumble notifications
    std::uint8_t gamepadnr;

    // Used as ID for adding/removinf devices from evdev notifications
    uinput_t::pointer dev;

    feedback_queue_t rumble_queue;

    int gain;

    // No need to send rumble data when old values equals the new values
    std::pair<std::uint32_t, std::uint32_t> old_rumble;

    std::unordered_map<int, data_t> id_to_data;
  };

  struct rumble_ctx_t {
    std::thread rumble_thread;

    safe::queue_t<mail_evdev_t> rumble_queue_queue;
  };

  void
  broadcastRumble(safe::queue_t<mail_evdev_t> &ctx);
  int
  startRumble(rumble_ctx_t &ctx) {
    ctx.rumble_thread = std::thread { broadcastRumble, std::ref(ctx.rumble_queue_queue) };

    return 0;
  }

  void
  stopRumble(rumble_ctx_t &ctx) {
    ctx.rumble_queue_queue.stop();

    BOOST_LOG(debug) << "Waiting for Gamepad notifications to stop..."sv;
    ctx.rumble_thread.join();
    BOOST_LOG(debug) << "Gamepad notifications stopped"sv;
  }

  static auto notifications = safe::make_shared<rumble_ctx_t>(startRumble, stopRumble);

  struct input_raw_t {
  public:
    void
    clear_touchscreen() {
      std::filesystem::path touch_path { appdata() / "sunshine_touchscreen"sv };

      if (std::filesystem::is_symlink(touch_path)) {
        std::filesystem::remove(touch_path);
      }

      touch_input.reset();
    }

    void
    clear_keyboard() {
      std::filesystem::path key_path { appdata() / "sunshine_keyboard"sv };

      if (std::filesystem::is_symlink(key_path)) {
        std::filesystem::remove(key_path);
      }

      keyboard_input.reset();
    }

    void
    clear_mouse() {
      std::filesystem::path mouse_path { appdata() / "sunshine_mouse"sv };

      if (std::filesystem::is_symlink(mouse_path)) {
        std::filesystem::remove(mouse_path);
      }

      mouse_input.reset();
    }

    void
    clear_gamepad(int nr) {
      auto &[dev, _] = gamepads[nr];

      if (!dev) {
        return;
      }

      // Remove this gamepad from notifications
      rumble_ctx->rumble_queue_queue.raise(nr, dev.get(), nullptr, pollfd_t {});

      std::stringstream ss;

      ss << "sunshine_gamepad_"sv << nr;

      auto gamepad_path = platf::appdata() / ss.str();
      if (std::filesystem::is_symlink(gamepad_path)) {
        std::filesystem::remove(gamepad_path);
      }

      gamepads[nr] = std::make_pair(uinput_t {}, gamepad_state_t {});
    }

    int
    create_mouse() {
      int err = libevdev_uinput_create_from_device(mouse_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &mouse_input);

      if (err) {
        BOOST_LOG(error) << "Could not create Sunshine Mouse: "sv << strerror(-err);
        return -1;
      }

      std::filesystem::create_symlink(libevdev_uinput_get_devnode(mouse_input.get()), appdata() / "sunshine_mouse"sv);

      return 0;
    }

    int
    create_touchscreen() {
      int err = libevdev_uinput_create_from_device(touch_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &touch_input);

      if (err) {
        BOOST_LOG(error) << "Could not create Sunshine Touchscreen: "sv << strerror(-err);
        return -1;
      }

      std::filesystem::create_symlink(libevdev_uinput_get_devnode(touch_input.get()), appdata() / "sunshine_touchscreen"sv);

      return 0;
    }

    int
    create_keyboard() {
      int err = libevdev_uinput_create_from_device(keyboard_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &keyboard_input);

      if (err) {
        BOOST_LOG(error) << "Could not create Sunshine Keyboard: "sv << strerror(-err);
        return -1;
      }

      std::filesystem::create_symlink(libevdev_uinput_get_devnode(keyboard_input.get()), appdata() / "sunshine_keyboard"sv);

      return 0;
    }

    /**
     * @brief Creates a new virtual gamepad.
     * @param id The gamepad ID.
     * @param metadata Controller metadata from client (empty if none provided).
     * @param feedback_queue The queue for posting messages back to the client.
     * @return 0 on success.
     */
    int
    alloc_gamepad(const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t &&feedback_queue) {
      TUPLE_2D_REF(input, gamepad_state, gamepads[id.globalIndex]);

      int err = libevdev_uinput_create_from_device(gamepad_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &input);

      gamepad_state = gamepad_state_t {};

      if (err) {
        BOOST_LOG(error) << "Could not create Sunshine Gamepad: "sv << strerror(-err);
        return -1;
      }

      std::stringstream ss;
      ss << "sunshine_gamepad_"sv << id.globalIndex;
      auto gamepad_path = platf::appdata() / ss.str();

      if (std::filesystem::is_symlink(gamepad_path)) {
        std::filesystem::remove(gamepad_path);
      }

      auto dev_node = libevdev_uinput_get_devnode(input.get());

      rumble_ctx->rumble_queue_queue.raise(
        id.clientRelativeIndex,
        input.get(),
        std::move(feedback_queue),
        pollfd_t {
          dup(libevdev_uinput_get_fd(input.get())),
          (std::int16_t) POLLIN,
          (std::int16_t) 0,
        });

      std::filesystem::create_symlink(dev_node, gamepad_path);
      return 0;
    }

    void
    clear() {
      clear_touchscreen();
      clear_keyboard();
      clear_mouse();
      for (int x = 0; x < gamepads.size(); ++x) {
        clear_gamepad(x);
      }

#ifdef SUNSHINE_BUILD_X11
      if (display) {
        x11::CloseDisplay(display);
        display = nullptr;
      }
#endif
    }

    ~input_raw_t() {
      clear();
    }

    safe::shared_t<rumble_ctx_t>::ptr_t rumble_ctx;

    std::vector<std::pair<uinput_t, gamepad_state_t>> gamepads;
    uinput_t mouse_input;
    uinput_t touch_input;
    uinput_t keyboard_input;

    evdev_t gamepad_dev;
    evdev_t touch_dev;
    evdev_t mouse_dev;
    evdev_t keyboard_dev;

#ifdef SUNSHINE_BUILD_X11
    Display *display;
#endif
  };

  inline void
  rumbleIterate(std::vector<effect_t> &effects, std::vector<pollfd_t> &polls, std::chrono::milliseconds to) {
    std::vector<pollfd> polls_recv;
    polls_recv.reserve(polls.size());
    for (auto &poll : polls) {
      polls_recv.emplace_back(poll.el);
    }

    auto res = poll(polls_recv.data(), polls_recv.size(), to.count());

    // If timed out
    if (!res) {
      return;
    }

    if (res < 0) {
      char err_str[1024];
      BOOST_LOG(error) << "Couldn't poll Gamepad file descriptors: "sv << strerror_r(errno, err_str, 1024);

      return;
    }

    for (int x = 0; x < polls.size(); ++x) {
      auto poll = std::begin(polls) + x;
      auto effect_it = std::begin(effects) + x;

      auto fd = (*poll)->fd;

      // TUPLE_2D_REF(dev, q, *dev_q_it);

      // on error
      if (polls_recv[x].revents & (POLLHUP | POLLRDHUP | POLLERR)) {
        BOOST_LOG(warning) << "Gamepad ["sv << x << "] file descriptor closed unexpectedly"sv;

        polls.erase(poll);
        effects.erase(effect_it);

        --x;
        continue;
      }

      if (!(polls_recv[x].revents & POLLIN)) {
        continue;
      }

      input_event events[64];

      // Read all available events
      auto bytes = read(fd, &events, sizeof(events));

      if (bytes < 0) {
        char err_str[1024];

        BOOST_LOG(error) << "Couldn't read evdev input ["sv << errno << "]: "sv << strerror_r(errno, err_str, 1024);

        polls.erase(poll);
        effects.erase(effect_it);

        --x;
        continue;
      }

      if (bytes < sizeof(input_event)) {
        BOOST_LOG(warning) << "Reading evdev input: Expected at least "sv << sizeof(input_event) << " bytes, got "sv << bytes << " instead"sv;
        continue;
      }

      auto event_count = bytes / sizeof(input_event);

      for (auto event = events; event != (events + event_count); ++event) {
        switch (event->type) {
          case EV_FF:
            // BOOST_LOG(debug) << "EV_FF: "sv << event->value << " aka "sv << util::hex(event->value).to_string_view();

            if (event->code == FF_GAIN) {
              BOOST_LOG(debug) << "EV_FF: code [FF_GAIN]: value: "sv << event->value << " aka "sv << util::hex(event->value).to_string_view();
              effect_it->gain = std::clamp(event->value, 0, 0xFFFF);

              break;
            }

            BOOST_LOG(debug) << "EV_FF: id ["sv << event->code << "]: value: "sv << event->value << " aka "sv << util::hex(event->value).to_string_view();

            if (event->value) {
              effect_it->activate(event->code);
            }
            else {
              effect_it->deactivate(event->code);
            }
            break;
          case EV_UINPUT:
            switch (event->code) {
              case UI_FF_UPLOAD: {
                uinput_ff_upload upload {};

                // *VERY* important, without this you break
                // the kernel and have to reboot due to dead
                // hanging process
                upload.request_id = event->value;

                ioctl(fd, UI_BEGIN_FF_UPLOAD, &upload);
                auto fg = util::fail_guard([&]() {
                  upload.retval = 0;
                  ioctl(fd, UI_END_FF_UPLOAD, &upload);
                });

                effect_it->upload(upload.effect);
              } break;
              case UI_FF_ERASE: {
                uinput_ff_erase erase {};

                // *VERY* important, without this you break
                // the kernel and have to reboot due to dead
                // hanging process
                erase.request_id = event->value;

                ioctl(fd, UI_BEGIN_FF_ERASE, &erase);
                auto fg = util::fail_guard([&]() {
                  erase.retval = 0;
                  ioctl(fd, UI_END_FF_ERASE, &erase);
                });

                effect_it->erase(erase.effect_id);
              } break;
            }
            break;
          default:
            BOOST_LOG(debug)
              << util::hex(event->type).to_string_view() << ": "sv
              << util::hex(event->code).to_string_view() << ": "sv
              << event->value << " aka "sv << util::hex(event->value).to_string_view();
        }
      }
    }
  }

  void
  broadcastRumble(safe::queue_t<mail_evdev_t> &rumble_queue_queue) {
    std::vector<effect_t> effects;
    std::vector<pollfd_t> polls;

    while (rumble_queue_queue.running()) {
      while (rumble_queue_queue.peek()) {
        auto dev_rumble_queue = rumble_queue_queue.pop();

        if (!dev_rumble_queue) {
          // rumble_queue_queue is no longer running
          return;
        }

        auto gamepadnr = std::get<0>(*dev_rumble_queue);
        auto dev = std::get<1>(*dev_rumble_queue);
        auto &rumble_queue = std::get<2>(*dev_rumble_queue);
        auto &pollfd = std::get<3>(*dev_rumble_queue);

        {
          auto effect_it = std::find_if(std::begin(effects), std::end(effects), [dev](auto &curr_effect) {
            return dev == curr_effect.dev;
          });

          if (effect_it != std::end(effects)) {
            auto poll_it = std::begin(polls) + (effect_it - std::begin(effects));

            polls.erase(poll_it);
            effects.erase(effect_it);

            BOOST_LOG(debug) << "Removed Gamepad device from notifications"sv;

            continue;
          }

          // There may be an attepmt to remove, that which not exists
          if (!rumble_queue) {
            BOOST_LOG(warning) << "Attempting to remove a gamepad device from notifications that isn't already registered"sv;
            continue;
          }
        }

        polls.emplace_back(std::move(pollfd));
        effects.emplace_back(gamepadnr, dev, std::move(rumble_queue));

        BOOST_LOG(debug) << "Added Gamepad device to notifications"sv;
      }

      if (polls.empty()) {
        std::this_thread::sleep_for(250ms);
      }
      else {
        rumbleIterate(effects, polls, 100ms);

        auto now = std::chrono::steady_clock::now();
        for (auto &effect : effects) {
          TUPLE_2D(old_weak, old_strong, effect.old_rumble);
          TUPLE_2D(weak, strong, effect.rumble(now));

          if (old_weak != weak || old_strong != strong) {
            BOOST_LOG(debug) << "Sending haptic feedback: lowfreq [0x"sv << util::hex(strong).to_string_view() << "]: highfreq [0x"sv << util::hex(weak).to_string_view() << ']';

            effect.rumble_queue->raise(gamepad_feedback_msg_t::make_rumble(effect.gamepadnr, strong, weak));
          }
        }
      }
    }
  }

  /**
   * @brief XTest absolute mouse move.
   * @param input The input_t instance to use.
   * @param x Absolute x position.
   * @param y Absolute y position.
   *
   * EXAMPLES:
   * ```cpp
   * x_abs_mouse(input, 0, 0);
   * ```
   */
  static void
  x_abs_mouse(input_t &input, float x, float y) {
#ifdef SUNSHINE_BUILD_X11
    Display *xdisplay = ((input_raw_t *) input.get())->display;
    if (!xdisplay) {
      return;
    }
    x11::tst::FakeMotionEvent(xdisplay, -1, x, y, CurrentTime);
    x11::Flush(xdisplay);
#endif
  }

  /**
   * @brief Absolute mouse move.
   * @param input The input_t instance to use.
   * @param touch_port The touch_port instance to use.
   * @param x Absolute x position.
   * @param y Absolute y position.
   *
   * EXAMPLES:
   * ```cpp
   * abs_mouse(input, touch_port, 0, 0);
   * ```
   */
  void
  abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
    auto touchscreen = ((input_raw_t *) input.get())->touch_input.get();
    if (!touchscreen) {
      x_abs_mouse(input, x, y);
      return;
    }

    auto scaled_x = (int) std::lround((x + touch_port.offset_x) * ((float) target_touch_port.width / (float) touch_port.width));
    auto scaled_y = (int) std::lround((y + touch_port.offset_y) * ((float) target_touch_port.height / (float) touch_port.height));

    libevdev_uinput_write_event(touchscreen, EV_ABS, ABS_X, scaled_x);
    libevdev_uinput_write_event(touchscreen, EV_ABS, ABS_Y, scaled_y);
    libevdev_uinput_write_event(touchscreen, EV_KEY, BTN_TOOL_FINGER, 1);
    libevdev_uinput_write_event(touchscreen, EV_KEY, BTN_TOOL_FINGER, 0);

    libevdev_uinput_write_event(touchscreen, EV_SYN, SYN_REPORT, 0);
  }

  /**
   * @brief XTest relative mouse move.
   * @param input The input_t instance to use.
   * @param deltaX Relative x position.
   * @param deltaY Relative y position.
   *
   * EXAMPLES:
   * ```cpp
   * x_move_mouse(input, 10, 10);  // Move mouse 10 pixels down and right
   * ```
   */
  static void
  x_move_mouse(input_t &input, int deltaX, int deltaY) {
#ifdef SUNSHINE_BUILD_X11
    Display *xdisplay = ((input_raw_t *) input.get())->display;
    if (!xdisplay) {
      return;
    }
    x11::tst::FakeRelativeMotionEvent(xdisplay, deltaX, deltaY, CurrentTime);
    x11::Flush(xdisplay);
#endif
  }

  /**
   * @brief Relative mouse move.
   * @param input The input_t instance to use.
   * @param deltaX Relative x position.
   * @param deltaY Relative y position.
   *
   * EXAMPLES:
   * ```cpp
   * move_mouse(input, 10, 10); // Move mouse 10 pixels down and right
   * ```
   */
  void
  move_mouse(input_t &input, int deltaX, int deltaY) {
    auto mouse = ((input_raw_t *) input.get())->mouse_input.get();
    if (!mouse) {
      x_move_mouse(input, deltaX, deltaY);
      return;
    }

    if (deltaX) {
      libevdev_uinput_write_event(mouse, EV_REL, REL_X, deltaX);
    }

    if (deltaY) {
      libevdev_uinput_write_event(mouse, EV_REL, REL_Y, deltaY);
    }

    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }

  /**
   * @brief XTest mouse button press/release.
   * @param input The input_t instance to use.
   * @param button Which mouse button to emulate.
   * @param release Whether the event was a press (false) or a release (true)
   *
   * EXAMPLES:
   * ```cpp
   * x_button_mouse(input, 1, false); // Press left mouse button
   * ```
   */
  static void
  x_button_mouse(input_t &input, int button, bool release) {
#ifdef SUNSHINE_BUILD_X11
    unsigned int x_button = 0;
    switch (button) {
      case BUTTON_LEFT:
        x_button = 1;
        break;
      case BUTTON_MIDDLE:
        x_button = 2;
        break;
      case BUTTON_RIGHT:
        x_button = 3;
        break;
      default:
        x_button = (button - 4) + 8;  // Button 4 (Moonlight) starts at index 8 (X11)
        break;
    }

    if (x_button < 1 || x_button > 31) {
      return;
    }

    Display *xdisplay = ((input_raw_t *) input.get())->display;
    if (!xdisplay) {
      return;
    }
    x11::tst::FakeButtonEvent(xdisplay, x_button, !release, CurrentTime);
    x11::Flush(xdisplay);
#endif
  }

  /**
   * @brief Mouse button press/release.
   * @param input The input_t instance to use.
   * @param button Which mouse button to emulate.
   * @param release Whether the event was a press (false) or a release (true)
   *
   * EXAMPLES:
   * ```cpp
   * button_mouse(input, 1, false);  // Press left mouse button
   * ```
   */
  void
  button_mouse(input_t &input, int button, bool release) {
    auto mouse = ((input_raw_t *) input.get())->mouse_input.get();
    if (!mouse) {
      x_button_mouse(input, button, release);
      return;
    }

    int btn_type;
    int scan;

    if (button == 1) {
      btn_type = BTN_LEFT;
      scan = 90001;
    }
    else if (button == 2) {
      btn_type = BTN_MIDDLE;
      scan = 90003;
    }
    else if (button == 3) {
      btn_type = BTN_RIGHT;
      scan = 90002;
    }
    else if (button == 4) {
      btn_type = BTN_SIDE;
      scan = 90004;
    }
    else {
      btn_type = BTN_EXTRA;
      scan = 90005;
    }

    libevdev_uinput_write_event(mouse, EV_MSC, MSC_SCAN, scan);
    libevdev_uinput_write_event(mouse, EV_KEY, btn_type, release ? 0 : 1);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }

  /**
   * @brief XTest mouse scroll.
   * @param input The input_t instance to use.
   * @param distance How far to scroll.
   * @param button_pos Which mouse button to emulate for positive scroll.
   * @param button_neg Which mouse button to emulate for negative scroll.
   *
   * EXAMPLES:
   * ```cpp
   * x_scroll(input, 10, 4, 5);
   * ```
   */
  static void
  x_scroll(input_t &input, int distance, int button_pos, int button_neg) {
#ifdef SUNSHINE_BUILD_X11
    Display *xdisplay = ((input_raw_t *) input.get())->display;
    if (!xdisplay) {
      return;
    }

    const int button = distance > 0 ? button_pos : button_neg;
    for (int i = 0; i < abs(distance); i++) {
      x11::tst::FakeButtonEvent(xdisplay, button, true, CurrentTime);
      x11::tst::FakeButtonEvent(xdisplay, button, false, CurrentTime);
    }
    x11::Flush(xdisplay);
#endif
  }

  /**
   * @brief Vertical mouse scroll.
   * @param input The input_t instance to use.
   * @param high_res_distance How far to scroll.
   *
   * EXAMPLES:
   * ```cpp
   * scroll(input, 1200);
   * ```
   */
  void
  scroll(input_t &input, int high_res_distance) {
    int distance = high_res_distance / 120;

    auto mouse = ((input_raw_t *) input.get())->mouse_input.get();
    if (!mouse) {
      x_scroll(input, distance, 4, 5);
      return;
    }

    libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL, distance);
    libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL_HI_RES, high_res_distance);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }

  /**
   * @brief Horizontal mouse scroll.
   * @param input The input_t instance to use.
   * @param high_res_distance How far to scroll.
   *
   * EXAMPLES:
   * ```cpp
   * hscroll(input, 1200);
   * ```
   */
  void
  hscroll(input_t &input, int high_res_distance) {
    int distance = high_res_distance / 120;

    auto mouse = ((input_raw_t *) input.get())->mouse_input.get();
    if (!mouse) {
      x_scroll(input, distance, 6, 7);
      return;
    }

    libevdev_uinput_write_event(mouse, EV_REL, REL_HWHEEL, distance);
    libevdev_uinput_write_event(mouse, EV_REL, REL_HWHEEL_HI_RES, high_res_distance);
    libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
  }

  static keycode_t
  keysym(std::uint16_t modcode) {
    if (modcode <= keycodes.size()) {
      return keycodes[modcode];
    }

    return {};
  }

  /**
   * @brief XTest keyboard emulation.
   * @param input The input_t instance to use.
   * @param modcode The moonlight key code.
   * @param release Whether the event was a press (false) or a release (true).
   * @param flags SS_KBE_FLAG_* values.
   *
   * EXAMPLES:
   * ```cpp
   * x_keyboard(input, 0x5A, false, 0);  // Press Z
   * ```
   */
  static void
  x_keyboard(input_t &input, uint16_t modcode, bool release, uint8_t flags) {
#ifdef SUNSHINE_BUILD_X11
    auto keycode = keysym(modcode);
    if (keycode.keysym == UNKNOWN) {
      return;
    }

    Display *xdisplay = ((input_raw_t *) input.get())->display;
    if (!xdisplay) {
      return;
    }

    const auto keycode_x = XKeysymToKeycode(xdisplay, keycode.keysym);
    if (keycode_x == 0) {
      return;
    }

    x11::tst::FakeKeyEvent(xdisplay, keycode_x, !release, CurrentTime);
    x11::Flush(xdisplay);
#endif
  }

  /**
   * @brief Keyboard emulation.
   * @param input The input_t instance to use.
   * @param modcode The moonlight key code.
   * @param release Whether the event was a press (false) or a release (true).
   * @param flags SS_KBE_FLAG_* values.
   *
   * EXAMPLES:
   * ```cpp
   * keyboard(input, 0x5A, false, 0);  // Press Z
   * ```
   */
  void
  keyboard(input_t &input, uint16_t modcode, bool release, uint8_t flags) {
    auto keyboard = ((input_raw_t *) input.get())->keyboard_input.get();
    if (!keyboard) {
      x_keyboard(input, modcode, release, flags);
      return;
    }

    auto keycode = keysym(modcode);
    if (keycode.keycode == UNKNOWN) {
      return;
    }

    if (keycode.scancode != UNKNOWN) {
      libevdev_uinput_write_event(keyboard, EV_MSC, MSC_SCAN, keycode.scancode);
    }

    libevdev_uinput_write_event(keyboard, EV_KEY, keycode.keycode, release ? 0 : 1);
    libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);
  }

  void
  keyboard_ev(libevdev_uinput *keyboard, int linux_code, int event_code = 1) {
    libevdev_uinput_write_event(keyboard, EV_KEY, linux_code, event_code);
    libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);
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
   * Here we receive a single UTF-8 encoded char at a time,
   * the trick is to convert it to UTF-32 then send CTRL+SHIFT+U+{HEXCODE} in order to produce any
   * unicode character, see: https://en.wikipedia.org/wiki/Unicode_input
   *
   * ex:
   * - when receiving UTF-8 [0xF0 0x9F 0x91 0xB1] (which is '👱')
   * - we'll convert it to UTF-32 [0x1F471]
   * - then type: CTRL+SHIFT+U+1F471
   * see the conversion at: https://www.compart.com/en/unicode/U+1F471
   */
  void
  unicode(input_t &input, char *utf8, int size) {
    auto kb = ((input_raw_t *) input.get())->keyboard_input.get();
    if (!kb) {
      return;
    }

    /* Reading input text as UTF-8 */
    auto utf8_str = boost::locale::conv::to_utf<wchar_t>(utf8, utf8 + size, "UTF-8");
    /* Converting to UTF-32 */
    auto utf32_str = boost::locale::conv::utf_to_utf<char32_t>(utf8_str);
    /* To HEX string */
    auto hex_unicode = to_hex(utf32_str);
    BOOST_LOG(debug) << "Unicode, typing U+"sv << hex_unicode;

    /* pressing <CTRL> + <SHIFT> + U */
    keyboard_ev(kb, KEY_LEFTCTRL, 1);
    keyboard_ev(kb, KEY_LEFTSHIFT, 1);
    keyboard_ev(kb, KEY_U, 1);
    keyboard_ev(kb, KEY_U, 0);

    /* input each HEX character */
    for (auto &ch : hex_unicode) {
      auto key_str = "KEY_"s + ch;
      auto keycode = libevdev_event_code_from_name(EV_KEY, key_str.c_str());
      if (keycode == -1) {
        BOOST_LOG(warning) << "Unicode, unable to find keycode for: "sv << ch;
      }
      else {
        keyboard_ev(kb, keycode, 1);
        keyboard_ev(kb, keycode, 0);
      }
    }

    /* releasing <SHIFT> and <CTRL> */
    keyboard_ev(kb, KEY_LEFTSHIFT, 0);
    keyboard_ev(kb, KEY_LEFTCTRL, 0);
  }

  /**
   * @brief Creates a new virtual gamepad.
   * @param input The global input context.
   * @param id The gamepad ID.
   * @param metadata Controller metadata from client (empty if none provided).
   * @param feedback_queue The queue for posting messages back to the client.
   * @return 0 on success.
   */
  int
  alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    return ((input_raw_t *) input.get())->alloc_gamepad(id, metadata, std::move(feedback_queue));
  }

  void
  free_gamepad(input_t &input, int nr) {
    ((input_raw_t *) input.get())->clear_gamepad(nr);
  }

  void
  gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    TUPLE_2D_REF(uinput, gamepad_state_old, ((input_raw_t *) input.get())->gamepads[nr]);

    auto bf = gamepad_state.buttonFlags ^ gamepad_state_old.buttonFlags;
    auto bf_new = gamepad_state.buttonFlags;

    if (bf) {
      // up pressed == -1, down pressed == 1, else 0
      if ((DPAD_UP | DPAD_DOWN) & bf) {
        int button_state = bf_new & DPAD_UP ? -1 : (bf_new & DPAD_DOWN ? 1 : 0);

        libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_HAT0Y, button_state);
      }

      if ((DPAD_LEFT | DPAD_RIGHT) & bf) {
        int button_state = bf_new & DPAD_LEFT ? -1 : (bf_new & DPAD_RIGHT ? 1 : 0);

        libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_HAT0X, button_state);
      }

      if (START & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_START, bf_new & START ? 1 : 0);
      if (BACK & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_SELECT, bf_new & BACK ? 1 : 0);
      if (LEFT_STICK & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_THUMBL, bf_new & LEFT_STICK ? 1 : 0);
      if (RIGHT_STICK & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_THUMBR, bf_new & RIGHT_STICK ? 1 : 0);
      if (LEFT_BUTTON & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_TL, bf_new & LEFT_BUTTON ? 1 : 0);
      if (RIGHT_BUTTON & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_TR, bf_new & RIGHT_BUTTON ? 1 : 0);
      if ((HOME | MISC_BUTTON) & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_MODE, bf_new & (HOME | MISC_BUTTON) ? 1 : 0);
      if (A & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_SOUTH, bf_new & A ? 1 : 0);
      if (B & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_EAST, bf_new & B ? 1 : 0);
      if (X & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_NORTH, bf_new & X ? 1 : 0);
      if (Y & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_WEST, bf_new & Y ? 1 : 0);
    }

    if (gamepad_state_old.lt != gamepad_state.lt) {
      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_Z, gamepad_state.lt);
    }

    if (gamepad_state_old.rt != gamepad_state.rt) {
      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_RZ, gamepad_state.rt);
    }

    if (gamepad_state_old.lsX != gamepad_state.lsX) {
      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_X, gamepad_state.lsX);
    }

    if (gamepad_state_old.lsY != gamepad_state.lsY) {
      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_Y, -gamepad_state.lsY);
    }

    if (gamepad_state_old.rsX != gamepad_state.rsX) {
      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_RX, gamepad_state.rsX);
    }

    if (gamepad_state_old.rsY != gamepad_state.rsY) {
      libevdev_uinput_write_event(uinput.get(), EV_ABS, ABS_RY, -gamepad_state.rsY);
    }

    gamepad_state_old = gamepad_state;
    libevdev_uinput_write_event(uinput.get(), EV_SYN, SYN_REPORT, 0);
  }

  /**
   * @brief Allocates a context to store per-client input data.
   * @param input The global input context.
   * @return A unique pointer to a per-client input data context.
   */
  std::unique_ptr<client_input_t>
  allocate_client_input_context(input_t &input) {
    // Unused
    return nullptr;
  }

  /**
   * @brief Sends a touch event to the OS.
   * @param input The client-specific input context.
   * @param touch_port The current viewport for translating to screen coordinates.
   * @param touch The touch event.
   */
  void
  touch(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch) {
    // Unimplemented feature - platform_caps::pen_touch
  }

  /**
   * @brief Sends a pen event to the OS.
   * @param input The client-specific input context.
   * @param touch_port The current viewport for translating to screen coordinates.
   * @param pen The pen event.
   */
  void
  pen(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen) {
    // Unimplemented feature - platform_caps::pen_touch
  }

  /**
   * @brief Sends a gamepad touch event to the OS.
   * @param input The global input context.
   * @param touch The touch event.
   */
  void
  gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    // Unimplemented feature - platform_caps::controller_touch
  }

  /**
   * @brief Sends a gamepad motion event to the OS.
   * @param input The global input context.
   * @param motion The motion event.
   */
  void
  gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    // Unimplemented
  }

  /**
   * @brief Sends a gamepad battery event to the OS.
   * @param input The global input context.
   * @param battery The battery event.
   */
  void
  gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    // Unimplemented
  }

  /**
   * @brief Initialize a new keyboard and return it.
   *
   * EXAMPLES:
   * ```cpp
   * auto my_keyboard = keyboard();
   * ```
   */
  evdev_t
  keyboard() {
    evdev_t dev { libevdev_new() };

    libevdev_set_uniq(dev.get(), "Sunshine Keyboard");
    libevdev_set_id_product(dev.get(), 0xDEAD);
    libevdev_set_id_vendor(dev.get(), 0xBEEF);
    libevdev_set_id_bustype(dev.get(), 0x3);
    libevdev_set_id_version(dev.get(), 0x111);
    libevdev_set_name(dev.get(), "Keyboard passthrough");

    libevdev_enable_event_type(dev.get(), EV_KEY);
    for (const auto &keycode : keycodes) {
      libevdev_enable_event_code(dev.get(), EV_KEY, keycode.keycode, nullptr);
    }
    libevdev_enable_event_type(dev.get(), EV_MSC);
    libevdev_enable_event_code(dev.get(), EV_MSC, MSC_SCAN, nullptr);

    return dev;
  }

  /**
   * @brief Initialize a new `uinput` virtual mouse and return it.
   *
   * EXAMPLES:
   * ```cpp
   * auto my_mouse = mouse();
   * ```
   */
  evdev_t
  mouse() {
    evdev_t dev { libevdev_new() };

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

  /**
   * @brief Initialize a new `uinput` virtual touchscreen and return it.
   *
   * EXAMPLES:
   * ```cpp
   * auto my_touchscreen = touchscreen();
   * ```
   */
  evdev_t
  touchscreen() {
    evdev_t dev { libevdev_new() };

    libevdev_set_uniq(dev.get(), "Sunshine Touch");
    libevdev_set_id_product(dev.get(), 0xDEAD);
    libevdev_set_id_vendor(dev.get(), 0xBEEF);
    libevdev_set_id_bustype(dev.get(), 0x3);
    libevdev_set_id_version(dev.get(), 0x111);
    libevdev_set_name(dev.get(), "Touchscreen passthrough");

    libevdev_enable_property(dev.get(), INPUT_PROP_DIRECT);

    libevdev_enable_event_type(dev.get(), EV_KEY);
    libevdev_enable_event_code(dev.get(), EV_KEY, BTN_TOUCH, nullptr);
    libevdev_enable_event_code(dev.get(), EV_KEY, BTN_TOOL_PEN, nullptr);  // Needed to be enabled for BTN_TOOL_FINGER to work.
    libevdev_enable_event_code(dev.get(), EV_KEY, BTN_TOOL_FINGER, nullptr);

    input_absinfo absx {
      0,
      0,
      target_touch_port.width,
      1,
      0,
      28
    };

    input_absinfo absy {
      0,
      0,
      target_touch_port.height,
      1,
      0,
      28
    };
    libevdev_enable_event_type(dev.get(), EV_ABS);
    libevdev_enable_event_code(dev.get(), EV_ABS, ABS_X, &absx);
    libevdev_enable_event_code(dev.get(), EV_ABS, ABS_Y, &absy);

    return dev;
  }

  /**
   * @brief Initialize a new `uinput` virtual X360 gamepad and return it.
   *
   * EXAMPLES:
   * ```cpp
   * auto my_x360 = x360();
   * ```
   */
  evdev_t
  x360() {
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

    libevdev_enable_event_type(dev.get(), EV_FF);
    libevdev_enable_event_code(dev.get(), EV_FF, FF_RUMBLE, nullptr);
    libevdev_enable_event_code(dev.get(), EV_FF, FF_CONSTANT, nullptr);
    libevdev_enable_event_code(dev.get(), EV_FF, FF_PERIODIC, nullptr);
    libevdev_enable_event_code(dev.get(), EV_FF, FF_SINE, nullptr);
    libevdev_enable_event_code(dev.get(), EV_FF, FF_RAMP, nullptr);
    libevdev_enable_event_code(dev.get(), EV_FF, FF_GAIN, nullptr);

    return dev;
  }

  /**
   * @brief Initialize the input system and return it.
   *
   * EXAMPLES:
   * ```cpp
   * auto my_input = input();
   * ```
   */
  input_t
  input() {
    input_t result { new input_raw_t() };
    auto &gp = *(input_raw_t *) result.get();

    gp.rumble_ctx = notifications.ref();

    gp.gamepads.resize(MAX_GAMEPADS);

    // Ensure starting from clean slate
    gp.clear();
    gp.keyboard_dev = keyboard();
    gp.touch_dev = touchscreen();
    gp.mouse_dev = mouse();
    gp.gamepad_dev = x360();

    gp.create_mouse();
    gp.create_touchscreen();
    gp.create_keyboard();

    // If we do not have a keyboard, touchscreen, or mouse, fall back to XTest
    if (!gp.mouse_input || !gp.touch_input || !gp.keyboard_input) {
      BOOST_LOG(error) << "Unable to create some input devices! Are you a member of the 'input' group?"sv;

#ifdef SUNSHINE_BUILD_X11
      if (x11::init() || x11::tst::init()) {
        BOOST_LOG(error) << "Unable to initialize X11 and/or XTest fallback"sv;
      }
      else {
        BOOST_LOG(info) << "Falling back to XTest"sv;
        x11::InitThreads();
        gp.display = x11::OpenDisplay(NULL);
      }
#endif
    }

    return result;
  }

  void
  freeInput(void *p) {
    auto *input = (input_raw_t *) p;
    delete input;
  }

  std::vector<std::string_view> &
  supported_gamepads() {
    static std::vector<std::string_view> gamepads { "x360"sv };

    return gamepads;
  }

  /**
   * @brief Returns the supported platform capabilities to advertise to the client.
   * @return Capability flags.
   */
  platform_caps::caps_t
  get_capabilities() {
    return 0;
  }
}  // namespace platf
