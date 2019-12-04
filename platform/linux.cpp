//
// Created by loki on 6/21/19.
//

#include "common.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XTest.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include <iostream>
#include <bitset>

namespace platf {
using namespace std::literals;

using ifaddr_t = util::safe_ptr<ifaddrs, freeifaddrs>;

ifaddr_t get_ifaddrs() {
  ifaddrs *p { nullptr };

  getifaddrs(&p);

  return ifaddr_t { p };
}

std::string from_sockaddr(const sockaddr *const ip_addr) {
  char data[INET6_ADDRSTRLEN];

  auto family = ip_addr->sa_family;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6*)ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in*)ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
  }

  return std::string { data };
}

std::string get_local_ip(int family) {
  std::bitset<2> family_f {};

  if(family == 0) {
    family_f[0] = true;
    family_f[1] = true;
  }

  if(family == AF_INET) {
    family_f[0] = true;
  }

  if(family == AF_INET6) {
    family_f[1] = true;
  }


  std::string ip_addr;
  auto ifaddr = get_ifaddrs();
  for(auto pos = ifaddr.get(); pos != nullptr; pos = pos->ifa_next) {
    if(pos->ifa_addr && pos->ifa_flags & IFF_UP && !(pos->ifa_flags & IFF_LOOPBACK)) {
      if(
        (family_f[0] && pos->ifa_addr->sa_family == AF_INET) ||
        (family_f[1] && pos->ifa_addr->sa_family == AF_INET6)
        ){
        ip_addr = from_sockaddr(pos->ifa_addr);
	break;
      }
    }
  }

  return ip_addr;
}

std::string get_local_ip() { return get_local_ip(AF_INET); }

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

struct mic_attr_t {
  pa_sample_spec ss;
  util::safe_ptr<pa_simple, pa_simple_free> mic;
};

display_t display() {
  return display_t { new display_attr_t {} };
}

img_t snapshot(display_t &display_void) {
  auto &display = *((display_attr_t*)display_void.get());

  XImage *img { XGetImage(
    display.display,
    display.window,
    0, 0,
    display.attr.width, display.attr.height,
    AllPlanes, ZPixmap)
  };

  XFixesCursorImage *overlay = XFixesGetCursorImage(display.display);

  auto pixels = (int*)img->data;

  auto screen_height = display.attr.height;
  auto screen_width  = display.attr.width;

  auto delta_height = std::min<uint16_t>(overlay->height, std::abs(overlay->y - screen_height));
  auto delta_width = std::min<uint16_t>(overlay->width, std::abs(overlay->x - screen_width));
  for(auto y = 0; y < delta_height; ++y) {

    auto overlay_begin = &overlay->pixels[y * overlay->width];
    auto overlay_end   = &overlay->pixels[y * overlay->width + delta_width];

    auto pixels_begin = &pixels[(y + overlay->y - 1) * screen_width + overlay->x - 1];
    std::for_each(overlay_begin, overlay_end, [&](long pixel) {
      int *pixel_p = (int*)&pixel;

      if(pixel_p[0] != 0) {
        *pixels_begin = pixel_p[0];
      }
      ++pixels_begin;
    });
  }

  return img_t { img };
}

uint8_t *img_data(img_t &img) {
  return (uint8_t*)((XImage*)img.get())->data;
}

int32_t img_width(img_t &img) {
  return ((XImage*)img.get())->width;
}

int32_t img_height(img_t &img) {
  return ((XImage*)img.get())->height;
}

//FIXME: Pass frame_rate instead of hard coding it
mic_t microphone() {
  mic_t mic {
    new mic_attr_t { 
      { PA_SAMPLE_S16LE, 48000, 2 },
      { }
    }
  };

  int error;
  mic_attr_t *mic_attr = (mic_attr_t*)mic.get();
  mic_attr->mic.reset(
    pa_simple_new(nullptr, "sunshine", pa_stream_direction_t::PA_STREAM_RECORD, nullptr, "sunshine_record", &mic_attr->ss, nullptr, nullptr, &error)
  );

  if(!mic_attr->mic) {
    auto err_str = pa_strerror(error);
    std::cout << "pa_simple_new() failed: "sv << err_str << std::endl;

    exit(1);
  }

  return mic;
}

audio_t audio(mic_t &mic, std::uint32_t buf_size) {
  auto mic_attr = (mic_attr_t*)mic.get();

  audio_t result { new std::uint8_t[buf_size] };

  auto buf = (std::uint8_t*)result.get();
  int error;
  if(pa_simple_read(mic_attr->mic.get(), buf, buf_size, &error)) {
    std::cout << "pa_simple_read() failed: "sv << pa_strerror(error) << std::endl;
  }

  return result;
}

std::int16_t *audio_data(audio_t &audio) {
  return (int16_t*)audio.get();
}


void move_mouse(display_t::element_type *display, int deltaX, int deltaY) {
  auto &disp = *((display_attr_t*)display);

  XWarpPointer(disp.display, None, None, 0, 0, 0, 0, deltaX, deltaY);
  XFlush(disp.display);
}

void button_mouse(display_t::element_type *display, int button, bool release) {
  auto &disp = *((display_attr_t *) display);

  XTestFakeButtonEvent(disp.display, button, !release, CurrentTime);

  XFlush(disp.display);
}

void scroll(display_t::element_type *display, int distance) {
  auto &disp = *((display_attr_t *) display);

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

void keyboard(display_t::element_type *display, uint16_t modcode, bool release) {
  auto &disp = *((display_attr_t *) display);
  KeyCode kc = XKeysymToKeycode(disp.display, keysym(modcode));

  if(!kc) {
    return;
  }

  XTestFakeKeyEvent(disp.display, kc, !release, 0);

  XSync(disp.display, 0);
  XFlush(disp.display);
}

void freeDisplay(void*p) {
  delete (display_attr_t*)p;
}

void freeImage(void*p) {
  XDestroyImage((XImage*)p);
}

void freeMic(void*p) {
  delete (mic_attr_t*)p;
}

void freeAudio(void*p) {
  delete[] (std::uint8_t*)p;
}
}
