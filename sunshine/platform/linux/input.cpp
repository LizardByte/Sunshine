#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#include <cmath>
#include <cstring>
#include <filesystem>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

#include "sunshine/platform/common.h"

// Support older versions
#ifndef REL_HWHEEL_HI_RES
#define REL_HWHEEL_HI_RES 0x0c
#endif

#ifndef REL_WHEEL_HI_RES
#define REL_WHEEL_HI_RES 0x0b
#endif

using namespace std::literals;
namespace platf {
constexpr auto mail_evdev = "platf::evdev"sv;

using evdev_t  = util::safe_ptr<libevdev, libevdev_free>;
using uinput_t = util::safe_ptr<libevdev_uinput, libevdev_uinput_destroy>;

constexpr pollfd read_pollfd { -1, 0, 0 };
KITTY_USING_MOVE_T(pollfd_t, pollfd, read_pollfd, {
  if(el.fd >= 0) {
    ioctl(el.fd, EVIOCGRAB, (void *)0);

    close(el.fd);
  }
});

using mail_evdev_t = std::tuple<int, uinput_t::pointer, rumble_queue_t, pollfd_t>;

struct keycode_t {
  std::uint32_t keycode;
  std::uint32_t scancode;

  int pressed;
};

constexpr auto UNKNOWN = 0;

static constexpr std::array<keycode_t, 0xE3> init_keycodes() {
  std::array<keycode_t, 0xE3> keycodes {};

#define __CONVERT(wincode, linuxcode, scancode)                                       \
  static_assert(wincode < keycodes.size(), "Keycode doesn't fit into keycode array"); \
  static_assert(wincode >= 0, "Are you mad?, keycode needs to be greater than zero"); \
  keycodes[wincode] = keycode_t { linuxcode, scancode };

  constexpr auto VK_NUMPAD = 0x60;
  constexpr auto VK_F1     = 0x70;
  constexpr auto VK_F13    = 0x7C;
  constexpr auto VK_0      = 0x30;

  for(auto x = 0; x < 9; ++x) {
    keycodes[x + VK_NUMPAD + 1] = keycode_t { (std::uint32_t)KEY_KP1 + x, (std::uint32_t)0x70059 + x };
    keycodes[x + VK_0 + 1]      = keycode_t { (std::uint32_t)KEY_1 + x, (std::uint32_t)0x7001E + x };
  }

  for(auto x = 0; x < 10; ++x) {
    keycodes[x + VK_F1] = keycode_t { (std::uint32_t)KEY_F1 + x, (std::uint32_t)0x70046 + x };
  }

  for(auto x = 0; x < 12; ++x) {
    keycodes[x + VK_F13] = keycode_t { (std::uint32_t)KEY_F13 + x, (std::uint32_t)0x7003A + x };
  }

  __CONVERT(0x08 /* VKEY_BACK */, KEY_BACKSPACE, 0x7002A);
  __CONVERT(0x09 /* VKEY_TAB */, KEY_TAB, 0x7002B);
  __CONVERT(0x0C /* VKEY_CLEAR */, KEY_CLEAR, UNKNOWN);
  __CONVERT(0x0D /* VKEY_RETURN */, KEY_ENTER, 0x70028);
  __CONVERT(0x10 /* VKEY_SHIFT */, KEY_LEFTSHIFT, 0x700E1);
  __CONVERT(0x11 /* VKEY_CONTROL */, KEY_LEFTCTRL, 0x700E0);
  __CONVERT(0x12 /* VKEY_MENU */, KEY_LEFTALT, UNKNOWN);
  __CONVERT(0x13 /* VKEY_PAUSE */, KEY_PAUSE, UNKNOWN);
  __CONVERT(0x14 /* VKEY_CAPITAL */, KEY_CAPSLOCK, 0x70039);
  __CONVERT(0x15 /* VKEY_KANA */, KEY_KATAKANAHIRAGANA, UNKNOWN);
  __CONVERT(0x16 /* VKEY_HANGUL */, KEY_HANGEUL, UNKNOWN);
  __CONVERT(0x17 /* VKEY_JUNJA */, KEY_HANJA, UNKNOWN);
  __CONVERT(0x19 /* VKEY_KANJI */, KEY_KATAKANA, UNKNOWN);
  __CONVERT(0x1B /* VKEY_ESCAPE */, KEY_ESC, 0x70029);
  __CONVERT(0x20 /* VKEY_SPACE */, KEY_SPACE, 0x7002C);
  __CONVERT(0x21 /* VKEY_PRIOR */, KEY_PAGEUP, 0x7004B);
  __CONVERT(0x22 /* VKEY_NEXT */, KEY_PAGEDOWN, 0x7004E);
  __CONVERT(0x23 /* VKEY_END */, KEY_END, 0x7004D);
  __CONVERT(0x24 /* VKEY_HOME */, KEY_HOME, 0x7004A);
  __CONVERT(0x25 /* VKEY_LEFT */, KEY_LEFT, 0x70050);
  __CONVERT(0x26 /* VKEY_UP */, KEY_UP, 0x70052);
  __CONVERT(0x27 /* VKEY_RIGHT */, KEY_RIGHT, 0x7004F);
  __CONVERT(0x28 /* VKEY_DOWN */, KEY_DOWN, 0x70051);
  __CONVERT(0x29 /* VKEY_SELECT */, KEY_SELECT, UNKNOWN);
  __CONVERT(0x2A /* VKEY_PRINT */, KEY_PRINT, UNKNOWN);
  __CONVERT(0x2C /* VKEY_SNAPSHOT */, KEY_SYSRQ, 0x70046);
  __CONVERT(0x2D /* VKEY_INSERT */, KEY_INSERT, 0x70049);
  __CONVERT(0x2E /* VKEY_DELETE */, KEY_DELETE, 0x7004C);
  __CONVERT(0x2F /* VKEY_HELP */, KEY_HELP, UNKNOWN);
  __CONVERT(0x30 /* VKEY_0 */, KEY_0, 0x70027);
  __CONVERT(0x31 /* VKEY_1 */, KEY_1, 0x7001E);
  __CONVERT(0x32 /* VKEY_2 */, KEY_2, 0x7001F);
  __CONVERT(0x33 /* VKEY_3 */, KEY_3, 0x70020);
  __CONVERT(0x34 /* VKEY_4 */, KEY_4, 0x70021);
  __CONVERT(0x35 /* VKEY_5 */, KEY_5, 0x70022);
  __CONVERT(0x36 /* VKEY_6 */, KEY_6, 0x70023);
  __CONVERT(0x37 /* VKEY_7 */, KEY_7, 0x70024);
  __CONVERT(0x38 /* VKEY_8 */, KEY_8, 0x70025);
  __CONVERT(0x39 /* VKEY_9 */, KEY_9, 0x70026);
  __CONVERT(0x41 /* VKEY_A */, KEY_A, 0x70004);
  __CONVERT(0x42 /* VKEY_B */, KEY_B, 0x70005);
  __CONVERT(0x43 /* VKEY_C */, KEY_C, 0x70006);
  __CONVERT(0x44 /* VKEY_D */, KEY_D, 0x70007);
  __CONVERT(0x45 /* VKEY_E */, KEY_E, 0x70008);
  __CONVERT(0x46 /* VKEY_F */, KEY_F, 0x70009);
  __CONVERT(0x47 /* VKEY_G */, KEY_G, 0x7000A);
  __CONVERT(0x48 /* VKEY_H */, KEY_H, 0x7000B);
  __CONVERT(0x49 /* VKEY_I */, KEY_I, 0x7000C);
  __CONVERT(0x4A /* VKEY_J */, KEY_J, 0x7000D);
  __CONVERT(0x4B /* VKEY_K */, KEY_K, 0x7000E);
  __CONVERT(0x4C /* VKEY_L */, KEY_L, 0x7000F);
  __CONVERT(0x4D /* VKEY_M */, KEY_M, 0x70010);
  __CONVERT(0x4E /* VKEY_N */, KEY_N, 0x70011);
  __CONVERT(0x4F /* VKEY_O */, KEY_O, 0x70012);
  __CONVERT(0x50 /* VKEY_P */, KEY_P, 0x70013);
  __CONVERT(0x51 /* VKEY_Q */, KEY_Q, 0x70014);
  __CONVERT(0x52 /* VKEY_R */, KEY_R, 0x70015);
  __CONVERT(0x53 /* VKEY_S */, KEY_S, 0x70016);
  __CONVERT(0x54 /* VKEY_T */, KEY_T, 0x70017);
  __CONVERT(0x55 /* VKEY_U */, KEY_U, 0x70018);
  __CONVERT(0x56 /* VKEY_V */, KEY_V, 0x70019);
  __CONVERT(0x57 /* VKEY_W */, KEY_W, 0x7001A);
  __CONVERT(0x58 /* VKEY_X */, KEY_X, 0x7001B);
  __CONVERT(0x59 /* VKEY_Y */, KEY_Y, 0x7001C);
  __CONVERT(0x5A /* VKEY_Z */, KEY_Z, 0x7001D);
  __CONVERT(0x5B /* VKEY_LWIN */, KEY_LEFTMETA, 0x700E3);
  __CONVERT(0x5C /* VKEY_RWIN */, KEY_RIGHTMETA, 0x700E7);
  __CONVERT(0x5F /* VKEY_SLEEP */, KEY_SLEEP, UNKNOWN);
  __CONVERT(0x60 /* VKEY_NUMPAD0 */, KEY_KP0, 0x70062);
  __CONVERT(0x61 /* VKEY_NUMPAD1 */, KEY_KP1, 0x70059);
  __CONVERT(0x62 /* VKEY_NUMPAD2 */, KEY_KP2, 0x7005A);
  __CONVERT(0x63 /* VKEY_NUMPAD3 */, KEY_KP3, 0x7005B);
  __CONVERT(0x64 /* VKEY_NUMPAD4 */, KEY_KP4, 0x7005C);
  __CONVERT(0x65 /* VKEY_NUMPAD5 */, KEY_KP5, 0x7005D);
  __CONVERT(0x66 /* VKEY_NUMPAD6 */, KEY_KP6, 0x7005E);
  __CONVERT(0x67 /* VKEY_NUMPAD7 */, KEY_KP7, 0x7005F);
  __CONVERT(0x68 /* VKEY_NUMPAD8 */, KEY_KP8, 0x70060);
  __CONVERT(0x69 /* VKEY_NUMPAD9 */, KEY_KP9, 0x70061);
  __CONVERT(0x6A /* VKEY_MULTIPLY */, KEY_KPASTERISK, 0x70055);
  __CONVERT(0x6B /* VKEY_ADD */, KEY_KPPLUS, 0x70057);
  __CONVERT(0x6C /* VKEY_SEPARATOR */, KEY_KPCOMMA, UNKNOWN);
  __CONVERT(0x6D /* VKEY_SUBTRACT */, KEY_KPMINUS, 0x70056);
  __CONVERT(0x6E /* VKEY_DECIMAL */, KEY_KPDOT, 0x70063);
  __CONVERT(0x6F /* VKEY_DIVIDE */, KEY_KPSLASH, 0x70054);
  __CONVERT(0x7A /* VKEY_F11 */, KEY_F11, 70044);
  __CONVERT(0x7B /* VKEY_F12 */, KEY_F12, 70045);
  __CONVERT(0x90 /* VKEY_NUMLOCK */, KEY_NUMLOCK, 0x70053);
  __CONVERT(0x91 /* VKEY_SCROLL */, KEY_SCROLLLOCK, 0x70047);
  __CONVERT(0xA0 /* VKEY_LSHIFT */, KEY_LEFTSHIFT, 0x700E1);
  __CONVERT(0xA1 /* VKEY_RSHIFT */, KEY_RIGHTSHIFT, 0x700E5);
  __CONVERT(0xA2 /* VKEY_LCONTROL */, KEY_LEFTCTRL, 0x700E0);
  __CONVERT(0xA3 /* VKEY_RCONTROL */, KEY_RIGHTCTRL, 0x700E4);
  __CONVERT(0xA4 /* VKEY_LMENU */, KEY_LEFTALT, 0x7002E);
  __CONVERT(0xA5 /* VKEY_RMENU */, KEY_RIGHTALT, 0x700E6);
  __CONVERT(0xBA /* VKEY_OEM_1 */, KEY_SEMICOLON, 0x70033);
  __CONVERT(0xBB /* VKEY_OEM_PLUS */, KEY_EQUAL, 0x7002E);
  __CONVERT(0xBC /* VKEY_OEM_COMMA */, KEY_COMMA, 0x70036);
  __CONVERT(0xBD /* VKEY_OEM_MINUS */, KEY_MINUS, 0x7002D);
  __CONVERT(0xBE /* VKEY_OEM_PERIOD */, KEY_DOT, 0x70037);
  __CONVERT(0xBF /* VKEY_OEM_2 */, KEY_SLASH, 0x70038);
  __CONVERT(0xC0 /* VKEY_OEM_3 */, KEY_GRAVE, 0x70035);
  __CONVERT(0xDB /* VKEY_OEM_4 */, KEY_LEFTBRACE, 0x7002F);
  __CONVERT(0xDC /* VKEY_OEM_5 */, KEY_BACKSLASH, 0x70031);
  __CONVERT(0xDD /* VKEY_OEM_6 */, KEY_RIGHTBRACE, 0x70030);
  __CONVERT(0xDE /* VKEY_OEM_7 */, KEY_APOSTROPHE, 0x70034);
  __CONVERT(0xE2 /* VKEY_NON_US_BACKSLASH */, KEY_102ND, 0x70064);
#undef __CONVERT

  return keycodes;
}

static constexpr auto keycodes = init_keycodes();

constexpr touch_port_t target_touch_port {
  0, 0,
  19200, 12000
};

static std::pair<std::uint32_t, std::uint32_t> operator*(const std::pair<std::uint32_t, std::uint32_t> &l, int r) {
  return {
    l.first * r,
    l.second * r,
  };
}

static std::pair<std::uint32_t, std::uint32_t> operator/(const std::pair<std::uint32_t, std::uint32_t> &l, int r) {
  return {
    l.first / r,
    l.second / r,
  };
}

static std::pair<std::uint32_t, std::uint32_t> &operator+=(std::pair<std::uint32_t, std::uint32_t> &l, const std::pair<std::uint32_t, std::uint32_t> &r) {
  l.first += r.first;
  l.second += r.second;

  return l;
}

static inline void print(const ff_envelope &envelope) {
  BOOST_LOG(debug)
    << "Envelope:"sv << std::endl
    << "  attack_length: " << envelope.attack_length << std::endl
    << "  attack_level: " << envelope.attack_level << std::endl
    << "  fade_length: " << envelope.fade_length << std::endl
    << "  fade_level: " << envelope.fade_level;
}

static inline void print(const ff_replay &replay) {
  BOOST_LOG(debug)
    << "Replay:"sv << std::endl
    << "  length: "sv << replay.length << std::endl
    << "  delay: "sv << replay.delay;
}

static inline void print(const ff_trigger &trigger) {
  BOOST_LOG(debug)
    << "Trigger:"sv << std::endl
    << "  button: "sv << trigger.button << std::endl
    << "  interval: "sv << trigger.interval;
}

static inline void print(const ff_effect &effect) {
  BOOST_LOG(debug)
    << std::endl
    << std::endl
    << "Received rumble effect with id: ["sv << effect.id << ']';

  switch(effect.type) {
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

  effect_t(int gamepadnr, uinput_t::pointer dev, rumble_queue_t &&q)
      : gamepadnr { gamepadnr }, dev { dev }, rumble_queue { std::move(q) }, gain { 0xFFFF }, id_to_data {} {}

  class data_t {
  public:
    KITTY_DEFAULT_CONSTR(data_t)

    data_t(const ff_effect &effect)
        : delay { effect.replay.delay },
          length { effect.replay.length },
          end_point { std::chrono::steady_clock::time_point::min() },
          envelope {},
          start {},
          end {} {

      switch(effect.type) {
      case FF_CONSTANT:
        start.weak   = effect.u.constant.level;
        start.strong = effect.u.constant.level;
        end.weak     = effect.u.constant.level;
        end.strong   = effect.u.constant.level;

        envelope = effect.u.constant.envelope;
        break;
      case FF_PERIODIC:
        start.weak   = effect.u.periodic.magnitude;
        start.strong = effect.u.periodic.magnitude;
        end.weak     = effect.u.periodic.magnitude;
        end.strong   = effect.u.periodic.magnitude;

        envelope = effect.u.periodic.envelope;
        break;

      case FF_RAMP:
        start.weak   = effect.u.ramp.start_level;
        start.strong = effect.u.ramp.start_level;
        end.weak     = effect.u.ramp.end_level;
        end.strong   = effect.u.ramp.end_level;

        envelope = effect.u.ramp.envelope;
        break;

      case FF_RUMBLE:
        start.weak   = effect.u.rumble.weak_magnitude;
        start.strong = effect.u.rumble.strong_magnitude;
        end.weak     = effect.u.rumble.weak_magnitude;
        end.strong   = effect.u.rumble.strong_magnitude;
        break;

      default:
        BOOST_LOG(warning) << "Effect type ["sv << effect.id << "] not implemented"sv;
      }
    }

    std::uint32_t magnitude(std::chrono::milliseconds time_left, std::uint32_t start, std::uint32_t end) {
      auto rel = end - start;

      return start + (rel * time_left.count() / length.count());
    }

    std::pair<std::uint32_t, std::uint32_t> rumble(std::chrono::steady_clock::time_point tp) {
      if(end_point < tp) {
        return {};
      }

      auto time_left =
        std::chrono::duration_cast<std::chrono::milliseconds>(
          end_point - tp);

      // If it needs to be delayed'
      if(time_left > length) {
        return {};
      }

      auto t = length - time_left;

      auto weak   = magnitude(t, start.weak, end.weak);
      auto strong = magnitude(t, start.strong, end.strong);

      if(t.count() < envelope.attack_length) {
        weak   = (envelope.attack_level * t.count() + weak * (envelope.attack_length - t.count())) / envelope.attack_length;
        strong = (envelope.attack_level * t.count() + strong * (envelope.attack_length - t.count())) / envelope.attack_length;
      }
      else if(time_left.count() < envelope.fade_length) {
        auto dt = (t - length).count() + envelope.fade_length;

        weak   = (envelope.fade_level * dt + weak * (envelope.fade_length - dt)) / envelope.fade_length;
        strong = (envelope.fade_level * dt + strong * (envelope.fade_length - dt)) / envelope.fade_length;
      }

      return {
        weak, strong
      };
    }

    void activate() {
      end_point = std::chrono::steady_clock::now() + delay + length;
    }

    void deactivate() {
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

  std::pair<std::uint32_t, std::uint32_t> rumble(std::chrono::steady_clock::time_point tp) {
    std::pair<std::uint32_t, std::uint32_t> weak_strong {};
    for(auto &[_, data] : id_to_data) {
      weak_strong += data.rumble(tp);
    }

    std::clamp<std::uint32_t>(weak_strong.first, 0, 0xFFFF);
    std::clamp<std::uint32_t>(weak_strong.second, 0, 0xFFFF);

    old_rumble = weak_strong * gain / 0xFFFF;
    return old_rumble;
  }

  void upload(const ff_effect &effect) {
    print(effect);

    auto it = id_to_data.find(effect.id);

    if(it == std::end(id_to_data)) {
      id_to_data.emplace(effect.id, effect);
      return;
    }

    data_t data { effect };

    data.end_point = it->second.end_point;
    it->second     = data;
  }

  void activate(int id) {
    auto it = id_to_data.find(id);

    if(it != std::end(id_to_data)) {
      it->second.activate();
    }
  }

  void deactivate(int id) {
    auto it = id_to_data.find(id);

    if(it != std::end(id_to_data)) {
      it->second.deactivate();
    }
  }

  void erase(int id) {
    id_to_data.erase(id);
    BOOST_LOG(debug) << "Removed rumble effect id ["sv << id << ']';
  }

  // Used as ID for rumble notifications
  int gamepadnr;

  // Used as ID for adding/removinf devices from evdev notifications
  uinput_t::pointer dev;

  rumble_queue_t rumble_queue;

  int gain;

  // No need to send rumble data when old values equals the new values
  std::pair<std::uint32_t, std::uint32_t> old_rumble;

  std::unordered_map<int, data_t> id_to_data;
};

struct rumble_ctx_t {
  std::thread rumble_thread;

  safe::queue_t<mail_evdev_t> rumble_queue_queue;
};

void broadcastRumble(safe::queue_t<mail_evdev_t> &ctx);
int startRumble(rumble_ctx_t &ctx) {
  ctx.rumble_thread = std::thread { broadcastRumble, std::ref(ctx.rumble_queue_queue) };

  return 0;
}

void stopRumble(rumble_ctx_t &ctx) {
  ctx.rumble_queue_queue.stop();

  BOOST_LOG(debug) << "Waiting for Gamepad notifications to stop..."sv;
  ctx.rumble_thread.join();
  BOOST_LOG(debug) << "Gamepad notifications stopped"sv;
}

static auto notifications = safe::make_shared<rumble_ctx_t>(startRumble, stopRumble);

struct input_raw_t {
public:
  void clear_touchscreen() {
    std::filesystem::path touch_path { appdata() / "sunshine_touchscreen"sv };

    if(std::filesystem::is_symlink(touch_path)) {
      std::filesystem::remove(touch_path);
    }

    touch_input.reset();
  }

  void clear_keyboard() {
    std::filesystem::path key_path { appdata() / "sunshine_keyboard"sv };

    if(std::filesystem::is_symlink(key_path)) {
      std::filesystem::remove(key_path);
    }

    keyboard_input.reset();
  }

  void clear_mouse() {
    std::filesystem::path mouse_path { appdata() / "sunshine_mouse"sv };

    if(std::filesystem::is_symlink(mouse_path)) {
      std::filesystem::remove(mouse_path);
    }

    mouse_input.reset();
  }

  void clear_gamepad(int nr) {
    auto &[dev, _] = gamepads[nr];

    if(!dev) {
      return;
    }

    // Remove this gamepad from notifications
    rumble_ctx->rumble_queue_queue.raise(nr, dev.get(), nullptr, pollfd_t {});

    std::stringstream ss;

    ss << "sunshine_gamepad_"sv << nr;

    auto gamepad_path = platf::appdata() / ss.str();
    if(std::filesystem::is_symlink(gamepad_path)) {
      std::filesystem::remove(gamepad_path);
    }

    gamepads[nr] = std::make_pair(uinput_t {}, gamepad_state_t {});
  }

  int create_mouse() {
    int err = libevdev_uinput_create_from_device(mouse_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &mouse_input);

    if(err) {
      BOOST_LOG(error) << "Could not create Sunshine Mouse: "sv << strerror(-err);
      return -1;
    }

    std::filesystem::create_symlink(libevdev_uinput_get_devnode(mouse_input.get()), appdata() / "sunshine_mouse"sv);

    return 0;
  }

  int create_touchscreen() {
    int err = libevdev_uinput_create_from_device(touch_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &touch_input);

    if(err) {
      BOOST_LOG(error) << "Could not create Sunshine Touchscreen: "sv << strerror(-err);
      return -1;
    }

    std::filesystem::create_symlink(libevdev_uinput_get_devnode(touch_input.get()), appdata() / "sunshine_touchscreen"sv);

    return 0;
  }

  int create_keyboard() {
    int err = libevdev_uinput_create_from_device(keyboard_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &keyboard_input);

    if(err) {
      BOOST_LOG(error) << "Could not create Sunshine Keyboard: "sv << strerror(-err);
      return -1;
    }

    std::filesystem::create_symlink(libevdev_uinput_get_devnode(keyboard_input.get()), appdata() / "sunshine_keyboard"sv);

    return 0;
  }

  int alloc_gamepad(int nr, rumble_queue_t &&rumble_queue) {
    TUPLE_2D_REF(input, gamepad_state, gamepads[nr]);

    int err = libevdev_uinput_create_from_device(gamepad_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &input);

    gamepad_state = gamepad_state_t {};

    if(err) {
      BOOST_LOG(error) << "Could not create Sunshine Gamepad: "sv << strerror(-err);
      return -1;
    }

    std::stringstream ss;
    ss << "sunshine_gamepad_"sv << nr;
    auto gamepad_path = platf::appdata() / ss.str();

    if(std::filesystem::is_symlink(gamepad_path)) {
      std::filesystem::remove(gamepad_path);
    }

    auto dev_node = libevdev_uinput_get_devnode(input.get());

    rumble_ctx->rumble_queue_queue.raise(
      nr,
      input.get(),
      std::move(rumble_queue),
      pollfd_t {
        dup(libevdev_uinput_get_fd(input.get())),
        (std::int16_t)POLLIN,
        (std::int16_t)0,
      });

    std::filesystem::create_symlink(dev_node, gamepad_path);
    return 0;
  }

  void clear() {
    clear_touchscreen();
    clear_keyboard();
    clear_mouse();
    for(int x = 0; x < gamepads.size(); ++x) {
      clear_gamepad(x);
    }
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
};

inline void rumbleIterate(std::vector<effect_t> &effects, std::vector<pollfd_t> &polls, std::chrono::milliseconds to) {
  std::vector<pollfd> polls_tmp;
  polls_tmp.reserve(polls.size());
  for(auto &poll : polls) {
    polls_tmp.emplace_back(poll.el);
  }

  auto res = poll(polls_tmp.data(), polls.size(), to.count());

  // If timed out
  if(!res) {
    return;
  }

  if(res < 0) {
    char err_str[1024];
    BOOST_LOG(error) << "Couldn't poll Gamepad file descriptors: "sv << strerror_r(errno, err_str, 1024);

    return;
  }

  for(int x = 0; x < polls.size(); ++x) {
    auto poll      = std::begin(polls) + x;
    auto effect_it = std::begin(effects) + x;

    auto fd = (*poll)->fd;

    // TUPLE_2D_REF(dev, q, *dev_q_it);

    // on error
    if((*poll)->revents & (POLLHUP | POLLRDHUP | POLLERR)) {
      BOOST_LOG(warning) << "Gamepad ["sv << x << "] file discriptor closed unexpectedly"sv;

      polls.erase(poll);
      effects.erase(effect_it);

      continue;
    }

    if(!((*poll)->revents & POLLIN)) {
      continue;
    }

    input_event events[64];

    // Read all available events
    auto bytes = read(fd, &events, sizeof(events));

    if(bytes < 0) {
      char err_str[1024];

      BOOST_LOG(error) << "Couldn't read evdev input ["sv << errno << "]: "sv << strerror_r(errno, err_str, 1024);

      polls.erase(poll);
      effects.erase(effect_it);

      continue;
    }

    if(bytes < sizeof(input_event)) {
      BOOST_LOG(warning) << "Reading evdev input: Expected at least "sv << sizeof(input_event) << " bytes, got "sv << bytes << " instead"sv;
      continue;
    }

    auto event_count = bytes / sizeof(input_event);

    for(auto event = events; event != (events + event_count); ++event) {
      switch(event->type) {
      case EV_FF:
        // BOOST_LOG(debug) << "EV_FF: "sv << event->value << " aka "sv << util::hex(event->value).to_string_view();

        if(event->code == FF_GAIN) {
          BOOST_LOG(debug) << "EV_FF: code [FF_GAIN]: value: "sv << event->value << " aka "sv << util::hex(event->value).to_string_view();
          effect_it->gain = std::clamp(event->value, 0, 0xFFFF);

          break;
        }

        BOOST_LOG(debug) << "EV_FF: id ["sv << event->code << "]: value: "sv << event->value << " aka "sv << util::hex(event->value).to_string_view();

        if(event->value) {
          effect_it->activate(event->code);
        }
        else {
          effect_it->deactivate(event->code);
        }
        break;
      case EV_UINPUT:
        switch(event->code) {
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

void broadcastRumble(safe::queue_t<mail_evdev_t> &rumble_queue_queue) {
  std::vector<effect_t> effects;
  std::vector<pollfd_t> polls;

  while(rumble_queue_queue.running()) {
    while(rumble_queue_queue.peek()) {
      auto dev_rumble_queue = rumble_queue_queue.pop();

      if(!dev_rumble_queue) {
        // rumble_queue_queue is no longer running
        return;
      }

      auto gamepadnr     = std::get<0>(*dev_rumble_queue);
      auto dev           = std::get<1>(*dev_rumble_queue);
      auto &rumble_queue = std::get<2>(*dev_rumble_queue);
      auto &pollfd       = std::get<3>(*dev_rumble_queue);

      {
        auto effect_it = std::find_if(std::begin(effects), std::end(effects), [dev](auto &curr_effect) {
          return dev == curr_effect.dev;
        });

        if(effect_it != std::end(effects)) {

          auto poll_it = std::begin(polls) + (effect_it - std::begin(effects));

          polls.erase(poll_it);
          effects.erase(effect_it);

          BOOST_LOG(debug) << "Removed Gamepad device from notifications"sv;

          continue;
        }

        // There may be an attepmt to remove, that which not exists
        if(!rumble_queue) {
          BOOST_LOG(warning) << "Attempting to remove a gamepad device from notifications that isn't already registered"sv;
          continue;
        }
      }

      polls.emplace_back(std::move(pollfd));
      effects.emplace_back(gamepadnr, dev, std::move(rumble_queue));

      BOOST_LOG(debug) << "Added Gamepad device to notifications"sv;
    }

    if(polls.empty()) {
      std::this_thread::sleep_for(250ms);
    }
    else {
      rumbleIterate(effects, polls, 100ms);

      auto now = std::chrono::steady_clock::now();
      for(auto &effect : effects) {
        TUPLE_2D(old_weak, old_strong, effect.old_rumble);
        TUPLE_2D(weak, strong, effect.rumble(now));

        if(old_weak != weak || old_strong != strong) {
          BOOST_LOG(debug) << "Sending haptic feedback: lowfreq [0x"sv << util::hex(weak).to_string_view() << "]: highfreq [0x"sv << util::hex(strong).to_string_view() << ']';

          effect.rumble_queue->raise(effect.gamepadnr, weak, strong);
        }
      }
    }
  }
}


void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
  auto touchscreen = ((input_raw_t *)input.get())->touch_input.get();

  auto scaled_x = (int)std::lround((x + touch_port.offset_x) * ((float)target_touch_port.width / (float)touch_port.width));
  auto scaled_y = (int)std::lround((y + touch_port.offset_y) * ((float)target_touch_port.height / (float)touch_port.height));

  libevdev_uinput_write_event(touchscreen, EV_ABS, ABS_X, scaled_x);
  libevdev_uinput_write_event(touchscreen, EV_ABS, ABS_Y, scaled_y);
  libevdev_uinput_write_event(touchscreen, EV_KEY, BTN_TOOL_FINGER, 1);
  libevdev_uinput_write_event(touchscreen, EV_KEY, BTN_TOOL_FINGER, 0);

  libevdev_uinput_write_event(touchscreen, EV_SYN, SYN_REPORT, 0);
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
  auto mouse = ((input_raw_t *)input.get())->mouse_input.get();

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
    scan     = 90001;
  }
  else if(button == 2) {
    btn_type = BTN_MIDDLE;
    scan     = 90003;
  }
  else if(button == 3) {
    btn_type = BTN_RIGHT;
    scan     = 90002;
  }
  else if(button == 4) {
    btn_type = BTN_SIDE;
    scan     = 90004;
  }
  else {
    btn_type = BTN_EXTRA;
    scan     = 90005;
  }

  auto mouse = ((input_raw_t *)input.get())->mouse_input.get();
  libevdev_uinput_write_event(mouse, EV_MSC, MSC_SCAN, scan);
  libevdev_uinput_write_event(mouse, EV_KEY, btn_type, release ? 0 : 1);
  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

void scroll(input_t &input, int high_res_distance) {
  int distance = high_res_distance / 120;

  auto mouse = ((input_raw_t *)input.get())->mouse_input.get();
  libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL, distance);
  libevdev_uinput_write_event(mouse, EV_REL, REL_WHEEL_HI_RES, high_res_distance);
  libevdev_uinput_write_event(mouse, EV_SYN, SYN_REPORT, 0);
}

static keycode_t keysym(std::uint16_t modcode) {
  if(modcode <= keycodes.size()) {
    return keycodes[modcode];
  }

  return {};
}

void keyboard(input_t &input, uint16_t modcode, bool release) {
  auto keyboard = ((input_raw_t *)input.get())->keyboard_input.get();

  auto keycode = keysym(modcode);
  if(keycode.keycode == UNKNOWN) {
    return;
  }

  if(keycode.scancode != UNKNOWN && (release || !keycode.pressed)) {
    libevdev_uinput_write_event(keyboard, EV_MSC, MSC_SCAN, keycode.scancode);
  }

  libevdev_uinput_write_event(keyboard, EV_KEY, keycode.keycode, release ? 0 : (1 + keycode.pressed));
  libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);

  keycode.pressed = 1;
}

int alloc_gamepad(input_t &input, int nr, rumble_queue_t rumble_queue) {
  return ((input_raw_t *)input.get())->alloc_gamepad(nr, std::move(rumble_queue));
}

void free_gamepad(input_t &input, int nr) {
  ((input_raw_t *)input.get())->clear_gamepad(nr);
}

void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
  TUPLE_2D_REF(uinput, gamepad_state_old, ((input_raw_t *)input.get())->gamepads[nr]);


  auto bf     = gamepad_state.buttonFlags ^ gamepad_state_old.buttonFlags;
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

    if(START & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_START, bf_new & START ? 1 : 0);
    if(BACK & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_SELECT, bf_new & BACK ? 1 : 0);
    if(LEFT_STICK & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_THUMBL, bf_new & LEFT_STICK ? 1 : 0);
    if(RIGHT_STICK & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_THUMBR, bf_new & RIGHT_STICK ? 1 : 0);
    if(LEFT_BUTTON & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_TL, bf_new & LEFT_BUTTON ? 1 : 0);
    if(RIGHT_BUTTON & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_TR, bf_new & RIGHT_BUTTON ? 1 : 0);
    if(HOME & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_MODE, bf_new & HOME ? 1 : 0);
    if(A & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_SOUTH, bf_new & A ? 1 : 0);
    if(B & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_EAST, bf_new & B ? 1 : 0);
    if(X & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_NORTH, bf_new & X ? 1 : 0);
    if(Y & bf) libevdev_uinput_write_event(uinput.get(), EV_KEY, BTN_WEST, bf_new & Y ? 1 : 0);
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

evdev_t keyboard() {
  evdev_t dev { libevdev_new() };

  libevdev_set_uniq(dev.get(), "Sunshine Keyboard");
  libevdev_set_id_product(dev.get(), 0xDEAD);
  libevdev_set_id_vendor(dev.get(), 0xBEEF);
  libevdev_set_id_bustype(dev.get(), 0x3);
  libevdev_set_id_version(dev.get(), 0x111);
  libevdev_set_name(dev.get(), "Keyboard passthrough");

  libevdev_enable_event_type(dev.get(), EV_KEY);
  for(const auto &keycode : keycodes) {
    libevdev_enable_event_code(dev.get(), EV_KEY, keycode.keycode, nullptr);
  }
  libevdev_enable_event_type(dev.get(), EV_MSC);
  libevdev_enable_event_code(dev.get(), EV_MSC, MSC_SCAN, nullptr);

  return dev;
}

evdev_t mouse() {
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

evdev_t touchscreen() {
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
  libevdev_enable_event_code(dev.get(), EV_KEY, BTN_TOOL_PEN, nullptr); // Needed to be enabled for BTN_TOOL_FINGER to work.
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

  libevdev_enable_event_type(dev.get(), EV_FF);
  libevdev_enable_event_code(dev.get(), EV_FF, FF_RUMBLE, nullptr);
  libevdev_enable_event_code(dev.get(), EV_FF, FF_CONSTANT, nullptr);
  libevdev_enable_event_code(dev.get(), EV_FF, FF_PERIODIC, nullptr);
  libevdev_enable_event_code(dev.get(), EV_FF, FF_SINE, nullptr);
  libevdev_enable_event_code(dev.get(), EV_FF, FF_RAMP, nullptr);
  libevdev_enable_event_code(dev.get(), EV_FF, FF_GAIN, nullptr);

  return dev;
}

input_t input() {
  input_t result { new input_raw_t() };
  auto &gp = *(input_raw_t *)result.get();

  gp.rumble_ctx = notifications.ref();

  gp.gamepads.resize(MAX_GAMEPADS);

  // Ensure starting from clean slate
  gp.clear();
  gp.keyboard_dev = keyboard();
  gp.touch_dev    = touchscreen();
  gp.mouse_dev    = mouse();
  gp.gamepad_dev  = x360();

  // If we do not have a keyboard, gamepad or mouse, no input is possible and we should abort
  if(gp.create_mouse() || gp.create_touchscreen() || gp.create_keyboard()) {
    log_flush();
    std::abort();
  }

  return result;
}

void freeInput(void *p) {
  auto *input = (input_raw_t *)p;
  delete input;
}

std::vector<std::string_view> &supported_gamepads() {
  static std::vector<std::string_view> gamepads { "x360"sv };

  return gamepads;
}
} // namespace platf
