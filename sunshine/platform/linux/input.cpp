#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <cmath>
#include <cstring>
#include <filesystem>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

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

using keyboard_t = util::safe_ptr_v2<Display, int, XCloseDisplay>;

constexpr pollfd read_pollfd { -1, 0, 0 };
KITTY_USING_MOVE_T(pollfd_t, pollfd, read_pollfd, {
  if(el.fd >= 0) {
    ioctl(el.fd, EVIOCGRAB, (void *)0);

    close(el.fd);
  }
});

using mail_evdev_t = std::tuple<uinput_t::pointer, rumble_queue_t, pollfd_t>;

constexpr touch_port_t target_touch_port {
  0, 0,
  19200, 12000
};

static std::pair<int, int> operator*(const std::pair<int, int> &l, int r) {
  return {
    l.first * r,
    l.second * r,
  };
}

static std::pair<int, int> operator/(const std::pair<int, int> &l, int r) {
  return {
    l.first / r,
    l.second / r,
  };
}

static std::pair<int, int> &operator+=(std::pair<int, int> &l, const std::pair<int, int> &r) {
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

  effect_t(uinput_t::pointer dev, rumble_queue_t &&q)
      : dev { dev }, rumble_queue { std::move(q) }, gain { 0xFFFFFF }, id_to_data {} {}

  class data_t {
  public:
    KITTY_DEFAULT_CONSTR(data_t)

    data_t(const ff_effect &effect)
        : delay { effect.replay.delay },
          length { effect.replay.length },
          end_point {},
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

    int magnitude(std::chrono::milliseconds time_left, int start, int end) {
      auto rel = end - start;

      return start + (rel * time_left.count() / length.count());
    }

    std::pair<int, int> rumble(std::chrono::steady_clock::time_point tp) {
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

    std::chrono::milliseconds delay;
    std::chrono::milliseconds length;

    std::chrono::steady_clock::time_point end_point;

    ff_envelope envelope;
    struct {
      int weak, strong;
    } start;

    struct {
      int weak, strong;
    } end;
  };

  std::pair<int, int> rumble(std::chrono::steady_clock::time_point tp) {
    std::pair<int, int> weak_strong {};
    for(auto &[_, data] : id_to_data) {
      weak_strong += data.rumble(tp);
    }

    std::clamp(weak_strong.first, 0, 0xFFFFFF);
    std::clamp(weak_strong.second, 0, 0xFFFFFF);

    return weak_strong * gain / 0xFFFF;
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

  void erase(int id) {
    id_to_data.erase(id);
    BOOST_LOG(debug) << "Removed rumble effect id ["sv << id << ']';
  }

  // Used as ID for rumble notifications
  uinput_t::pointer dev;
  rumble_queue_t rumble_queue;

  int gain;

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
    std::filesystem::path touch_path { "sunshine_touchscreen"sv };

    if(std::filesystem::is_symlink(touch_path)) {
      std::filesystem::remove(touch_path);
    }

    touch_input.reset();
  }
  void clear_mouse() {
    std::filesystem::path mouse_path { "sunshine_mouse"sv };

    if(std::filesystem::is_symlink(mouse_path)) {
      std::filesystem::remove(mouse_path);
    }

    mouse_input.reset();
  }

  void clear_gamepad(int nr) {
    auto &[dev, _] = gamepads[nr];

    // Remove this gamepad from notifications
    rumble_ctx->rumble_queue_queue.raise(dev.get(), nullptr, pollfd {});

    std::stringstream ss;

    ss << "sunshine_gamepad_"sv << nr;

    std::filesystem::path gamepad_path { ss.str() };
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

    std::filesystem::create_symlink(libevdev_uinput_get_devnode(mouse_input.get()), "sunshine_mouse"sv);

    return 0;
  }

  int create_touchscreen() {
    int err = libevdev_uinput_create_from_device(touch_dev.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &touch_input);

    if(err) {
      BOOST_LOG(error) << "Could not create Sunshine Touchscreen: "sv << strerror(-err);
      return -1;
    }

    std::filesystem::create_symlink(libevdev_uinput_get_devnode(touch_input.get()), "sunshine_touchscreen"sv);

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
    std::filesystem::path gamepad_path { ss.str() };

    if(std::filesystem::is_symlink(gamepad_path)) {
      std::filesystem::remove(gamepad_path);
    }

    auto dev_node = libevdev_uinput_get_devnode(input.get());

    rumble_ctx->rumble_queue_queue.raise(
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

  evdev_t gamepad_dev;
  evdev_t touch_dev;
  evdev_t mouse_dev;

  keyboard_t keyboard;
};

inline void rumbleIterate(std::vector<effect_t> &effects, std::vector<pollfd_t> &polls, std::chrono::milliseconds to) {
  auto res = poll(&polls.data()->el, polls.size(), to.count());

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
        BOOST_LOG(debug) << "EV_FF: "sv << event->value << " aka "sv << util::hex(event->value).to_string_view();
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

      TUPLE_3D_REF(dev, rumble_queue, pollfd, *dev_rumble_queue);
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
          continue;
        }
      }

      polls.emplace_back(std::move(pollfd));
      effects.emplace_back(dev, std::move(rumble_queue));

      BOOST_LOG(debug) << "Added Gamepad device to notifications"sv;
    }

    if(polls.empty()) {
      std::this_thread::sleep_for(50ms);
    }
    else {
      rumbleIterate(effects, polls, 100ms);
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

uint16_t keysym(uint16_t modcode) {
  constexpr auto VK_NUMPAD = 0x60;
  constexpr auto VK_F1     = 0x70;

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
  case 0xA5:
    return XK_Alt_R;
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
  auto &keyboard = ((input_raw_t *)input.get())->keyboard;
  KeyCode kc     = XKeysymToKeycode(keyboard.get(), keysym(modcode));

  if(!kc) {
    return;
  }

  XTestFakeKeyEvent(keyboard.get(), kc, !release, 0);

  XSync(keyboard.get(), 0);
  XFlush(keyboard.get());
}

int alloc_gamepad(input_t &input, int nr, rumble_queue_t &&rumble_queue) {
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
  libevdev_enable_event_code(dev.get(), EV_FF, FF_RAMP, nullptr);
  libevdev_enable_event_code(dev.get(), EV_FF, FF_GAIN, nullptr);

  return dev;
}

input_t input() {
  input_t result { new input_raw_t() };
  auto &gp = *(input_raw_t *)result.get();

  gp.rumble_ctx = notifications.ref();

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
  gp.touch_dev   = touchscreen();
  gp.mouse_dev   = mouse();
  gp.gamepad_dev = x360();

  if(gp.create_mouse() || gp.create_touchscreen()) {
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
