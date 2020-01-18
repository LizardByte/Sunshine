#include <thread>

#include <windows.h>
#include <winuser.h>

#include <ViGEm/Client.h>

#include "sunshine/main.h"
#include "common.h"

namespace platf {
using namespace std::literals;

class vigem_t {
public:
  using client_t = util::safe_ptr<_VIGEM_CLIENT_T, vigem_free>;
  using target_t = util::safe_ptr<_VIGEM_TARGET_T, vigem_target_free>;

  int init() {
    VIGEM_ERROR status;

    client.reset(vigem_alloc());

    status = vigem_connect(client.get());
    if(!VIGEM_SUCCESS(status)) {
      BOOST_LOG(warning) << "Couldn't setup connection to ViGEm for gamepad support ["sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    x360.reset(vigem_target_x360_alloc());

    status = vigem_target_add(client.get(), x360.get());
    if(!VIGEM_SUCCESS(status)) {
      BOOST_LOG(error) << "Couldn't add Gamepad to ViGEm connection ["sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    return 0;
  }

  ~vigem_t() {
    if(client) {
      if(vigem_target_is_attached(x360.get())) {
        auto status = vigem_target_remove(client.get(), x360.get());
        if(!VIGEM_SUCCESS(status)) {
          BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
        }
      }

      vigem_disconnect(client.get());
    }
  }

  target_t x360;
  client_t client;
};

std::string get_local_ip() { return "192.168.0.119"s; }

input_t input() {
  input_t result { new vigem_t {} };

  auto vigem = (vigem_t*)result.get();
  if(vigem->init()) {
    return nullptr;
  }

  return result;
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
  INPUT i {};

  i.type = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags = MOUSEEVENTF_MOVE;
  mi.dx = deltaX;
  mi.dy = deltaY;

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send mouse movement input"sv;
  }
}

void button_mouse(input_t &input, int button, bool release) {
  constexpr SHORT KEY_STATE_DOWN = 0x8000;

  INPUT i {};

  i.type = INPUT_MOUSE;
  auto &mi = i.mi;

  int mouse_button;
  if(button == 1) {
    mi.dwFlags = release ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
    mouse_button = VK_LBUTTON;
  }
  else if(button == 2) {
    mi.dwFlags = release ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
    mouse_button = VK_MBUTTON;
  }
  else {
    mi.dwFlags = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
    mouse_button = VK_RBUTTON;
  }

  auto key_state = GetAsyncKeyState(mouse_button);
  bool key_state_down = (key_state & KEY_STATE_DOWN) != 0;
  if(key_state_down != release) {
    BOOST_LOG(warning) << "Button state of mouse_button ["sv << button << "] does not match the desired state"sv;

    return;
  }

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send mouse button input"sv;
  }
}

void scroll(input_t &input, int distance) {
  INPUT i {};

  i.type = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags = MOUSEEVENTF_WHEEL;
  mi.mouseData = distance;

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send moue movement input"sv;
  }
}

void keyboard(input_t &input, uint16_t modcode, bool release) {
  constexpr SHORT KEY_STATE_DOWN = 0x8000;

  if(modcode == VK_RMENU) {
    modcode = VK_LWIN;
  }

  auto key_state = GetAsyncKeyState(modcode);
  bool key_state_down = (key_state & KEY_STATE_DOWN) != 0;
  if(key_state_down != release) {
    BOOST_LOG(warning) << "Key state of vkey ["sv << util::hex(modcode).to_string_view() << "] does not match the desired state ["sv << (release ? "on]"sv : "off]"sv);

    return;
  }

  INPUT i {};
  i.type = INPUT_KEYBOARD;
  auto &ki = i.ki;

  // For some reason, MapVirtualKey(VK_LWIN, MAPVK_VK_TO_VSC) doesn't seem to work :/
  if(modcode != VK_LWIN && modcode != VK_RWIN) {
    ki.wScan = MapVirtualKey(modcode, MAPVK_VK_TO_VSC);
    ki.dwFlags = KEYEVENTF_SCANCODE;
  }
  else {
    ki.wVk = modcode;
  }

  if(release) {
    ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send moue movement input"sv;
  }
}

void gamepad(input_t &input, const gamepad_state_t &gamepad_state) {
  // If there is no gamepad support
  if(!input) {
    return;
  }

  auto vigem = (vigem_t*)input.get();
  auto &xusb = *(PXUSB_REPORT)&gamepad_state;

  auto status = vigem_target_x360_update(vigem->client.get(), vigem->x360.get(), xusb);
  if(!VIGEM_SUCCESS(status)) {
    BOOST_LOG(fatal) << "Couldn't send gamepad input to ViGEm ["sv << util::hex(status).to_string_view() << ']';

    log_flush();
    std::abort();
  }
}

namespace gp {
void dpad_y(input_t &input, int button_state) {} // up pressed == -1, down pressed == 1, else 0
void dpad_x(input_t &input, int button_state) {} // left pressed == -1, right pressed == 1, else 0
void start(input_t &input, int button_down) {}
void back(input_t &input, int button_down) {}
void left_stick(input_t &input, int button_down) {}
void right_stick(input_t &input, int button_down) {}
void left_button(input_t &input, int button_down) {}
void right_button(input_t &input, int button_down) {}
void home(input_t &input, int button_down) {}
void a(input_t &input, int button_down) {}
void b(input_t &input, int button_down) {}
void x(input_t &input, int button_down) {}
void y(input_t &input, int button_down) {}
void left_trigger(input_t &input, std::uint8_t abs_z) {}
void right_trigger(input_t &input, std::uint8_t abs_z) {}
void left_stick_x(input_t &input, std::int16_t x) {}
void left_stick_y(input_t &input, std::int16_t y) {}
void right_stick_x(input_t &input, std::int16_t x) {}
void right_stick_y(input_t &input, std::int16_t y) {}
void sync(input_t &input) {}
}

void freeInput(void *p) {
  auto vigem = (vigem_t*)p;

  delete vigem;
}
}
