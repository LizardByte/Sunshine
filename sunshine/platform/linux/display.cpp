//
// Created by loki on 6/21/19.
//

#include "sunshine/platform/common.h"

#include <fstream>
#include <bitset>

#include <arpa/inet.h>
#include <ifaddrs.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <xcb/shm.h>
#include <xcb/xfixes.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include "sunshine/task_pool.h"
#include "sunshine/config.h"
#include "sunshine/main.h"

namespace platf {
using namespace std::literals;

void freeImage(XImage *);
void freeX(XFixesCursorImage *);

using ifaddr_t = util::safe_ptr<ifaddrs, freeifaddrs>;
using xcb_connect_t = util::safe_ptr<xcb_connection_t, xcb_disconnect>;
using xcb_img_t = util::c_ptr<xcb_shm_get_image_reply_t>;
using xcb_cursor_img = util::c_ptr<xcb_xfixes_get_cursor_image_reply_t>;

using xdisplay_t = util::safe_ptr_v2<Display, int, XCloseDisplay>;
using ximg_t = util::safe_ptr<XImage, freeImage>;
using xcursor_t = util::safe_ptr<XFixesCursorImage, freeX>;

class shm_id_t {
public:
  shm_id_t() : id { -1 } {}
  shm_id_t(int id) : id {id } {}
  shm_id_t(shm_id_t &&other) noexcept : id(other.id) {
    other.id = -1;
  }

  ~shm_id_t() {
    if(id != -1) {
      shmctl(id, IPC_RMID, nullptr);
      id = -1;
    }
  }
  int id;
};

class shm_data_t {
public:
  shm_data_t() : data {(void*)-1 } {}
  shm_data_t(void *data) : data {data } {}

  shm_data_t(shm_data_t &&other) noexcept : data(other.data) {
    other.data = (void*)-1;
  }

  ~shm_data_t() {
    if((std::uintptr_t)data != -1) {
      shmdt(data);
      data = (void*)-1;
    }
  }

  void *data;
};

struct x11_img_t : public img_t {
  ximg_t img;
};

struct shm_img_t : public img_t {
  ~shm_img_t() override {
    delete[] data;
    data = nullptr;
  }
};

void blend_cursor(Display *display, img_t &img) {
  xcursor_t overlay { XFixesGetCursorImage(display) };

  if(!overlay) {
    BOOST_LOG(error) << "Couldn't get cursor from XFixesGetCursorImage"sv;
    return;
  }

  overlay->x -= overlay->xhot;
  overlay->y -= overlay->yhot;

  overlay->x = std::max((short)0, overlay->x);
  overlay->y = std::max((short)0, overlay->y);

  auto pixels = (int*)img.data;

  auto screen_height = img.height;
  auto screen_width  = img.width;

  auto delta_height = std::min<uint16_t>(overlay->height, std::max(0, screen_height - overlay->y));
  auto delta_width = std::min<uint16_t>(overlay->width, std::max(0, screen_width - overlay->x));
  for(auto y = 0; y < delta_height; ++y) {

    auto overlay_begin = &overlay->pixels[y * overlay->width];
    auto overlay_end   = &overlay->pixels[y * overlay->width + delta_width];

    auto pixels_begin = &pixels[(y + overlay->y) * (img.row_pitch / img.pixel_pitch) + overlay->x];
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
}
struct x11_attr_t : public display_t {
  x11_attr_t() : xdisplay {XOpenDisplay(nullptr) }, xwindow { }, xattr {} {
    if(!xdisplay) {
      BOOST_LOG(fatal) << "Could not open x11 display"sv;
      log_flush();
      std::abort();
    }

    xwindow = DefaultRootWindow(xdisplay.get());

    refresh();

    width  = xattr.width;
    height = xattr.height;
  }

  void refresh() {
    XGetWindowAttributes(xdisplay.get(), xwindow, &xattr);
  }

  capture_e snapshot(img_t *img_out_base, std::chrono::milliseconds timeout, bool cursor) override {
    refresh();

    if(width != xattr.width || height != xattr.height) {
      return capture_e::reinit;
    }

    XImage *img { XGetImage(
      xdisplay.get(),
      xwindow,
      0, 0,
      xattr.width, xattr.height,
      AllPlanes, ZPixmap)
    };

    auto img_out = (x11_img_t*)img_out_base;
    img_out->width = img->width;
    img_out->height = img->height;
    img_out->data = (uint8_t*)img->data;
    img_out->row_pitch = img->bytes_per_line;
    img_out->pixel_pitch = img->bits_per_pixel / 8;
    img_out->img.reset(img);

    if(cursor) {
      blend_cursor(xdisplay.get(), *img_out_base);
    }

    return capture_e::ok;
  }

  std::shared_ptr<img_t> alloc_img() override {
    return std::make_shared<x11_img_t>();
  }

  int dummy_img(img_t *img) override {
    snapshot(img, 0s, true);
    return 0;
  }

  xdisplay_t xdisplay;
  Window xwindow;
  XWindowAttributes xattr;
};

struct shm_attr_t : public x11_attr_t {
  xdisplay_t shm_xdisplay; // Prevent race condition with x11_attr_t::xdisplay
  xcb_connect_t xcb;
  xcb_screen_t *display;
  std::uint32_t seg;

  shm_id_t shm_id;

  shm_data_t data;

  util::TaskPool::task_id_t refresh_task_id;
  void delayed_refresh() {
    refresh();

    refresh_task_id = task_pool.pushDelayed(&shm_attr_t::delayed_refresh, 2s, this).task_id;
  }

  shm_attr_t() : x11_attr_t(), shm_xdisplay {XOpenDisplay(nullptr) } {
    refresh_task_id = task_pool.pushDelayed(&shm_attr_t::delayed_refresh, 2s, this).task_id;
  }

  ~shm_attr_t() override {
    while(!task_pool.cancel(refresh_task_id));
  }

  capture_e snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor) override {
    if(width != xattr.width || height != xattr.height) {
      return capture_e::reinit;
    }

    auto img_cookie = xcb_shm_get_image_unchecked(
      xcb.get(),
      display->root,
      0, 0,
      width, height,
      ~0,
      XCB_IMAGE_FORMAT_Z_PIXMAP,
      seg,
      0
    );

    xcb_img_t img_reply { xcb_shm_get_image_reply(xcb.get(), img_cookie, nullptr) };
    if(!img_reply) {
      BOOST_LOG(error) << "Could not get image reply"sv;
      return capture_e::reinit;
    }

    std::copy_n((std::uint8_t*)data.data, frame_size(), img->data);

    if(cursor) {
      blend_cursor(shm_xdisplay.get(), *img);
    }

    return capture_e::ok;
  }

  std::shared_ptr<img_t> alloc_img() override {
    auto img = std::make_shared<shm_img_t>();
    img->width  = width;
    img->height = height;
    img->pixel_pitch = 4;
    img->row_pitch = img->pixel_pitch * width;
    img->data = new std::uint8_t[height * img->row_pitch];

    return img;
  }

  int dummy_img(platf::img_t *img) override {
    return 0;
  }

  int init() {
    shm_xdisplay.reset(XOpenDisplay(nullptr));
    xcb.reset(xcb_connect(nullptr, nullptr));
    if(xcb_connection_has_error(xcb.get())) {
      return -1;
    }

    if(!xcb_get_extension_data(xcb.get(), &xcb_shm_id)->present) {
      BOOST_LOG(error) << "Missing SHM extension"sv;

      return -1;
    }

    auto iter = xcb_setup_roots_iterator(xcb_get_setup(xcb.get()));
    display = iter.data;
    seg = xcb_generate_id(xcb.get());

    shm_id.id = shmget(IPC_PRIVATE, frame_size(), IPC_CREAT | 0777);
    if(shm_id.id == -1) {
      BOOST_LOG(error) << "shmget failed"sv;
      return -1;
    }

    xcb_shm_attach(xcb.get(), seg, shm_id.id, false);
    data.data = shmat(shm_id.id, nullptr, 0);

    if ((uintptr_t)data.data == -1) {
      BOOST_LOG(error) << "shmat failed"sv;

      return -1;
    }

    width  = display->width_in_pixels;
    height = display->height_in_pixels;

    return 0;
  }

  std::uint32_t frame_size() {
    return width * height * 4;
  }
};

struct mic_attr_t : public mic_t {
  pa_sample_spec ss;
  util::safe_ptr<pa_simple, pa_simple_free> mic;

  explicit mic_attr_t(pa_sample_format format, std::uint32_t sample_rate, std::uint8_t channels) : ss { format, sample_rate, channels }, mic {} {}
  capture_e sample(std::vector<std::int16_t> &sample_buf) override {
    auto sample_size = sample_buf.size();

    auto buf = sample_buf.data();
    int status;
    if(pa_simple_read(mic.get(), buf, sample_size * 2, &status)) {
      BOOST_LOG(error) << "pa_simple_read() failed: "sv << pa_strerror(status);

      return capture_e::error;
    }

    return capture_e::ok;
  }
};

std::shared_ptr<display_t> shm_display() {
  auto shm = std::make_shared<shm_attr_t>();

  if(shm->init()) {
    return nullptr;
  }

  return shm;
}

std::shared_ptr<display_t> display(platf::dev_type_e hwdevice_type) {
  if(hwdevice_type != platf::dev_type_e::none) {
    return nullptr;
  }

  auto shm_disp = shm_display();

  if(!shm_disp) {
    return std::make_shared<x11_attr_t>();
  }

  return shm_disp;
}

std::unique_ptr<mic_t> microphone(std::uint32_t sample_rate) {
  auto mic = std::make_unique<mic_attr_t>(PA_SAMPLE_S16LE, sample_rate, 2);

  int status;

  const char *audio_sink = "@DEFAULT_MONITOR@";
  if(!config::audio.sink.empty()) {
    audio_sink = config::audio.sink.c_str();
  }

  mic->mic.reset(
    pa_simple_new(nullptr, "sunshine", pa_stream_direction_t::PA_STREAM_RECORD, audio_sink, "sunshine-record", &mic->ss, nullptr, nullptr, &status)
  );

  if(!mic->mic) {
    auto err_str = pa_strerror(status);
    BOOST_LOG(error) << "pa_simple_new() failed: "sv << err_str;

    log_flush();
    std::abort();
  }

  return mic;
}

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

std::string get_mac_address(const std::string_view &address) {
  auto ifaddrs = get_ifaddrs();
  for(auto pos = ifaddrs.get(); pos != nullptr; pos = pos->ifa_next) {
    if(pos->ifa_addr && address == from_sockaddr(pos->ifa_addr)) {
      std::ifstream mac_file("/sys/class/net/"s + pos->ifa_name + "/address");
      if(mac_file.good()) {
        std::string mac_address;
        std::getline(mac_file, mac_address);
        return mac_address;
      }
    }
  }
  BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
  return "00:00:00:00:00:00"s;
}

void freeImage(XImage *p) {
  XDestroyImage(p);
}
void freeX(XFixesCursorImage *p) {
  XFree(p);
}
}
