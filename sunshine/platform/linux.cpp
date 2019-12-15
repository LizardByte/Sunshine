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

void interrupt_process(std::uint64_t handle) {
  kill((pid_t)handle, SIGINT);
}

struct display_attr_t {
  display_attr_t() : display { XOpenDisplay(nullptr) }, window { DefaultRootWindow(display) }, attr {} {
    refresh();
  }

  ~display_attr_t() {
    XCloseDisplay(display);
  }

  void refresh() {
    XGetWindowAttributes(display, window, &attr);
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

  display.refresh();
  XImage *img { XGetImage(
    display.display,
    display.window,
    0, 0,
    display.attr.width, display.attr.height,
    AllPlanes, ZPixmap)
  };

  XFixesCursorImage *overlay = XFixesGetCursorImage(display.display);
  overlay->x -= overlay->xhot;
  overlay->y -= overlay->yhot;

  overlay->x = std::max((short)0, overlay->x);
  overlay->y = std::max((short)0, overlay->y);

  auto pixels = (int*)img->data;

  auto screen_height = display.attr.height;
  auto screen_width  = display.attr.width;

  auto delta_height = std::min<uint16_t>(overlay->height, std::max(0, screen_height - overlay->y));
  auto delta_width = std::min<uint16_t>(overlay->width, std::max(0, screen_width - overlay->x));
  for(auto y = 0; y < delta_height; ++y) {

    auto overlay_begin = &overlay->pixels[y * overlay->width];
    auto overlay_end   = &overlay->pixels[y * overlay->width + delta_width];

    auto pixels_begin = &pixels[(y + overlay->y) * screen_width + overlay->x];
    std::for_each(overlay_begin, overlay_end, [&](long pixel) {
      int *pixel_p = (int*)&pixel;

      auto colors_in = (uint8_t*)pixels_begin;

      auto alpha = (*(uint*)pixel_p) >> 24u;
      if(alpha == 255) {
        *pixels_begin = *pixel_p;
      }
      else {
        auto colors_out = (uint8_t*)pixel_p;
        colors_in[0] = colors_out[0] + (colors_in[0] * (255 - alpha) + 255/2) / 255;
        colors_in[1] = colors_out[1] + (colors_in[1] * (255 - alpha) + 255/2) / 255;
        colors_in[2] = colors_out[2] + (colors_in[2] * (255 - alpha) + 255/2) / 255;
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
