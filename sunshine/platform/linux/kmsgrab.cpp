#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/round_robin.h"
#include "sunshine/utility.h"

#include "graphics.h"

using namespace std::literals;

namespace platf {

namespace kms {
using plane_res_t = util::safe_ptr<drmModePlaneRes, drmModeFreePlaneResources>;
using plane_t     = util::safe_ptr<drmModePlane, drmModeFreePlane>;
using fb_t        = util::safe_ptr<drmModeFB, drmModeFreeFB>;
using fb2_t       = util::safe_ptr<drmModeFB2, drmModeFreeFB2>;

class plane_it_t : public util::it_wrap_t<plane_t::element_type, plane_it_t> {
public:
  plane_it_t(int fd, std::uint32_t *plane_p, std::uint32_t *end)
      : fd { fd }, plane_p { plane_p }, end { end } {
    inc();
  }

  plane_it_t(int fd, std::uint32_t *end)
      : fd { fd }, plane_p { end }, end { end } {}

  void inc() {
    this->plane.reset();

    for(; plane_p != end; ++plane_p) {
      plane_t plane = drmModeGetPlane(fd, *plane_p);

      if(!plane) {
        BOOST_LOG(error) << "Couldn't get drm plane ["sv << (end - plane_p) << "]: "sv << strerror(errno);
        continue;
      }

      // If this plane is unused
      if(plane->fb_id) {
        this->plane = util::make_shared<plane_t>(plane.release());

        // One last increment
        ++plane_p;
        break;
      }
    }
  }

  bool eq(const plane_it_t &other) const {
    return plane_p == other.plane_p;
  }

  plane_t::pointer get() {
    return plane.get();
  }

  int fd;
  std::uint32_t *plane_p;
  std::uint32_t *end;

  util::shared_t<plane_t> plane;
};

class card_t {
public:
  int init(const char *path) {
    fd.el = open(path, O_RDWR);

    if(fd.el < 0) {
      BOOST_LOG(error) << "Couldn't open: "sv << path << ": "sv << strerror(errno);
      return -1;
    }

    if(drmSetClientCap(fd.el, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
      BOOST_LOG(error) << "Couldn't expose some/all drm planes"sv;
      return -1;
    }

    plane_res.reset(drmModeGetPlaneResources(fd.el));
    if(!plane_res) {
      BOOST_LOG(error) << "Couldn't get drm plane resources"sv;
      return -1;
    }

    return 0;
  }

  fb_t fb(plane_t::pointer plane) {
    return drmModeGetFB(fd.el, plane->fb_id);
  }

  plane_t operator[](std::uint32_t index) {
    return drmModeGetPlane(fd.el, plane_res->planes[index]);
  }

  std::uint32_t count() {
    return plane_res->count_planes;
  }

  plane_it_t begin() const {
    return plane_it_t { fd.el, plane_res->planes, plane_res->planes + plane_res->count_planes };
  }

  plane_it_t end() const {
    return plane_it_t { fd.el, plane_res->planes + plane_res->count_planes };
  }


  egl::file_t fd;
  plane_res_t plane_res;
};

struct kms_img_t : public img_t {
  ~kms_img_t() override {
    delete[] data;
    data = nullptr;
  }
};

void print(plane_t::pointer plane, fb_t::pointer fb) {
  BOOST_LOG(debug)
    << "x("sv << plane->x
    << ") y("sv << plane->y
    << ") crtc_x("sv << plane->crtc_x
    << ") crtc_y("sv << plane->crtc_y
    << ") crtc_id("sv << plane->crtc_id
    << ')';

  BOOST_LOG(debug)
    << "Resolution: "sv << fb->width << 'x' << fb->height
    << ": Pitch: "sv << fb->pitch
    << ": bpp: "sv << fb->bpp
    << ": depth: "sv << fb->depth;

  std::stringstream ss;

  ss << "Format ["sv;
  std::for_each_n(plane->formats, plane->count_formats - 1, [&ss](auto format) {
    ss << util::view(format) << ", "sv;
  });

  ss << util::view(plane->formats[plane->count_formats - 1]) << ']';

  BOOST_LOG(debug) << ss.str();
}

class display_t : public platf::display_t {
public:
  display_t() : platf::display_t() {}

  int init(const std::string &display_name, int framerate) {
    if(!gbm::create_device) {
      BOOST_LOG(warning) << "libgbm not initialized"sv;
      return -1;
    }

    delay = std::chrono::nanoseconds { 1s } / framerate;

    constexpr auto path = "/dev/dri/card1";
    if(card.init(path)) {
      return -1;
    }

    int monitor_index = util::from_view(display_name);
    int monitor       = 0;

    int pitch;

    auto end = std::end(card);
    for(auto plane = std::begin(card); plane != end; ++plane) {
      if(monitor != monitor_index) {
        ++monitor;
        continue;
      }

      auto fb = card.fb(plane.get());
      if(!fb) {
        BOOST_LOG(error) << "Couldn't get drm fb for plane ["sv << plane->fb_id << "]: "sv << strerror(errno);
        return -1;
      }

      if(!fb->handle) {
        BOOST_LOG(error)
          << "Couldn't get handle for DRM Framebuffer ["sv << plane->fb_id << "]: Possibly not permitted: do [sudo setcap cap_sys_admin+ep sunshine]"sv;
        return -1;
      }

      auto status = drmPrimeHandleToFD(card.fd.el, fb->handle, 0 /* flags */, &fb_fd.el);
      if(status || fb_fd.el < 0) {
        BOOST_LOG(error) << "Couldn't get primary file descriptor for Framebuffer ["sv << fb->fb_id << "]: "sv << strerror(errno);
        continue;
      }

      BOOST_LOG(info) << "Found monitor for DRM screencasting"sv;
      kms::print(plane.get(), fb.get());

      width      = fb->width;
      height     = fb->height;
      pitch      = fb->pitch;
      env_width  = width;
      env_height = height;

      break;
    }

    if(monitor != monitor_index) {
      BOOST_LOG(error) << "Couldn't find monitor ["sv << monitor_index << ']';

      return -1;
    }

    gbm.reset(gbm::create_device(card.fd.el));
    if(!gbm) {
      BOOST_LOG(error) << "Couldn't create GBM device: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return -1;
    }

    display = egl::make_display(gbm.get());
    if(!display) {
      return -1;
    }

    auto ctx_opt = egl::make_ctx(display.get());
    if(!ctx_opt) {
      return -1;
    }

    ctx = std::move(*ctx_opt);

    auto rgb_opt = egl::import_source(display.get(),
      {
        fb_fd.el,
        width,
        height,
        0,
        pitch,
      });

    if(!rgb_opt) {
      return -1;
    }

    rgb = std::move(*rgb_opt);

    return 0;
  }

  capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<img_t> img, bool *cursor) {
    auto next_frame = std::chrono::steady_clock::now();

    while(img) {
      auto now = std::chrono::steady_clock::now();

      if(next_frame > now) {
        std::this_thread::sleep_for((next_frame - now) / 3 * 2);
      }
      while(next_frame > now) {
        now = std::chrono::steady_clock::now();
      }
      next_frame = now + delay;

      auto status = snapshot(img.get(), 1000ms, *cursor);
      switch(status) {
      case platf::capture_e::reinit:
      case platf::capture_e::error:
        return status;
      case platf::capture_e::timeout:
        std::this_thread::sleep_for(1ms);
        continue;
      case platf::capture_e::ok:
        img = snapshot_cb(img);
        break;
      default:
        BOOST_LOG(error) << "Unrecognized capture status ["sv << (int)status << ']';
        return status;
      }
    }

    return capture_e::ok;
  }

  capture_e snapshot(img_t *img_out_base, std::chrono::milliseconds timeout, bool cursor) {

    gl::ctx.BindTexture(GL_TEXTURE_2D, rgb->tex[0]);

    gl::ctx.GetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, img_out_base->data);

    return capture_e::ok;
  }

  std::shared_ptr<img_t> alloc_img() override {
    auto img         = std::make_shared<kms_img_t>();
    img->width       = width;
    img->height      = height;
    img->pixel_pitch = 4;
    img->row_pitch   = img->pixel_pitch * width;
    img->data        = new std::uint8_t[height * img->row_pitch];

    return img;
  }

  int dummy_img(platf::img_t *img) override {
    return 0;
  }

  std::chrono::nanoseconds delay;

  card_t card;
  egl::file_t fb_fd;

  gbm::gbm_t gbm;
  egl::display_t display;
  egl::ctx_t ctx;

  egl::rgb_t rgb;
};
} // namespace kms

std::shared_ptr<display_t> kms_display(mem_type_e hwdevice_type, const std::string &display_name, int framerate) {
  auto disp = std::make_shared<kms::display_t>();

  if(disp->init(display_name, framerate)) {
    return nullptr;
  }

  return disp;
}

// A list of names of displays accepted as display_name
std::vector<std::string> kms_display_names() {
  if(!gbm::create_device) {
    BOOST_LOG(warning) << "libgbm not initialized"sv;
    return {};
  }

  std::vector<std::string> display_names;

  kms::card_t card;
  if(card.init("/dev/dri/card1")) {
    return {};
  }

  int count = 0;

  auto end = std::end(card);
  for(auto plane = std::begin(card); plane != end; ++plane) {
    auto fb = card.fb(plane.get());
    if(!fb) {
      BOOST_LOG(error) << "Couldn't get drm fb for plane ["sv << plane->fb_id << "]: "sv << strerror(errno);
      continue;
    }

    if(!fb->handle) {
      BOOST_LOG(error)
        << "Couldn't get handle for DRM Framebuffer ["sv << plane->fb_id << "]: Possibly not permitted: do [sudo setcap cap_sys_admin+ep sunshine]"sv;
      break;
    }

    kms::print(plane.get(), fb.get());

    display_names.emplace_back(std::to_string(count++));
  }

  return display_names;
}

} // namespace platf