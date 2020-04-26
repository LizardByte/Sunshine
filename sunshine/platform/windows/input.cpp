#include <sstream>
#include <iomanip>

#include <ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
#include <winuser.h>
#include <iphlpapi.h>

#include <ViGEm/Client.h>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"

namespace platf {
using namespace std::literals;

using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

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

    x360s.resize(MAX_GAMEPADS);

    return 0;
  }

  int alloc_x360(int nr) {
    auto &x360 = x360s[nr];
    assert(!x360);

    x360.reset(vigem_target_x360_alloc());
    auto status = vigem_target_add(client.get(), x360.get());
    if(!VIGEM_SUCCESS(status)) {
      BOOST_LOG(error) << "Couldn't add Gamepad to ViGEm connection ["sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    return 0;
  }

  void free_target(int nr) {
    auto &x360 = x360s[nr];

    if(x360 && vigem_target_is_attached(x360.get())) {
      auto status = vigem_target_remove(client.get(), x360.get());
      if(!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
      }
    }

    x360.reset();
  }

  ~vigem_t() {
    if(client) {
      for(auto &x360 : x360s) {
        if(x360 && vigem_target_is_attached(x360.get())) {
          auto status = vigem_target_remove(client.get(), x360.get());
          if(!VIGEM_SUCCESS(status)) {
            BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
          }
        }
      }

      vigem_disconnect(client.get());
    }
  }

  std::vector<target_t> x360s;
  client_t client;
};

std::string from_sockaddr(const sockaddr *const socket_address) {
  char data[INET6_ADDRSTRLEN];

  auto family = socket_address->sa_family;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6*)socket_address)->sin6_addr, data, INET6_ADDRSTRLEN);
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in*)socket_address)->sin_addr, data, INET_ADDRSTRLEN);
  }

  return std::string { data };
}

std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const ip_addr) {
  char data[INET6_ADDRSTRLEN];

  auto family = ip_addr->sa_family;
  std::uint16_t port;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6*)ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
    port = ((sockaddr_in6*)ip_addr)->sin6_port;
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in*)ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
    port = ((sockaddr_in*)ip_addr)->sin_port;
  }

  return { port, std::string { data } };
}

adapteraddrs_t get_adapteraddrs() {
  adapteraddrs_t info { nullptr };
  ULONG size = 0;

  while(GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, info.get(), &size) == ERROR_BUFFER_OVERFLOW) {
    info.reset((PIP_ADAPTER_ADDRESSES)malloc(size));
  }

  return info;
}

std::string get_mac_address(const std::string_view &address) {
  adapteraddrs_t info = get_adapteraddrs();
  for(auto adapter_pos = info.get(); adapter_pos != nullptr; adapter_pos = adapter_pos->Next) {
    for(auto addr_pos = adapter_pos->FirstUnicastAddress; addr_pos != nullptr; addr_pos = addr_pos->Next) {
      if(adapter_pos->PhysicalAddressLength != 0 && address == from_sockaddr(addr_pos->Address.lpSockaddr)) {
        std::stringstream mac_addr;
        mac_addr << std::hex;
        for(int i = 0; i < adapter_pos->PhysicalAddressLength; i++) {
          if(i > 0) {
            mac_addr << ':';
          }
          mac_addr << std::setw(2) << std::setfill('0') << (int)adapter_pos->PhysicalAddress[i];
        }
        return mac_addr.str();
      }
    }
  }
  BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
  return "00:00:00:00:00:00"s;
}

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
  else if(button == 3) {
    mi.dwFlags = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
    mouse_button = VK_RBUTTON;
  }
  else if(button == 4) {
    mi.dwFlags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
    mi.mouseData = XBUTTON1;
    mouse_button = VK_XBUTTON1;
  }
  else {
    mi.dwFlags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
    mi.mouseData = XBUTTON2;
    mouse_button = VK_XBUTTON2;
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
  if(modcode == VK_RMENU) {
    modcode = VK_LWIN;
  }

  INPUT i {};
  i.type = INPUT_KEYBOARD;
  auto &ki = i.ki;

  // For some reason, MapVirtualKey(VK_LWIN, MAPVK_VK_TO_VSC) doesn't seem to work :/
  if(modcode != VK_LWIN && modcode != VK_RWIN && modcode != VK_PAUSE) {
    ki.wScan = MapVirtualKey(modcode, MAPVK_VK_TO_VSC);
    ki.dwFlags = KEYEVENTF_SCANCODE;
  }
  else {
    ki.wVk = modcode;
  }

  // https://docs.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input#keystroke-message-flags
  switch(modcode) {
    case VK_RMENU:
    case VK_RCONTROL:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_DIVIDE:
      ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      break;
    default:
      break;
  }

  if(release) {
    ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send moue movement input"sv;
  }
}

int alloc_gamepad(input_t &input, int nr) {
  if(!input) {
    return 0;
  }

  return ((vigem_t*)input.get())->alloc_x360(nr);
}

void free_gamepad(input_t &input, int nr) {
  if(!input) {
    return;
  }

  ((vigem_t*)input.get())->free_target(nr);
}
void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
  // If there is no gamepad support
  if(!input) {
    return;
  }

  auto vigem = (vigem_t*)input.get();

  auto &xusb = *(PXUSB_REPORT)&gamepad_state;
  auto &x360 = vigem->x360s[nr];

  auto status = vigem_target_x360_update(vigem->client.get(), x360.get(), xusb);
  if(!VIGEM_SUCCESS(status)) {
    BOOST_LOG(fatal) << "Couldn't send gamepad input to ViGEm ["sv << util::hex(status).to_string_view() << ']';

    log_flush();
    std::abort();
  }
}

int thread_priority()  {
  return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) ? 0 : 1;
}

void freeInput(void *p) {
  auto vigem = (vigem_t*)p;

  delete vigem;
}
}
