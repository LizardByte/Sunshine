#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

#include "graphics.h"

using namespace std::literals;

namespace platf {

namespace kms {
using plane_res_t = util::safe_ptr<drmModePlaneRes, drmModeFreePlaneResources>;
using plane_t     = util::safe_ptr<drmModePlane, drmModeFreePlane>;
using fb_t        = util::safe_ptr<drmModeFB, drmModeFreeFB>;
using fb2_t       = util::safe_ptr<drmModeFB2, drmModeFreeFB2>;

struct kms_img_t : public img_t {
  ~kms_img_t() override {
    delete[] data;
    data = nullptr;
  }
};

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

    fd.el = open(path, O_RDWR);

    if(fd.el < 0) {
      BOOST_LOG(error) << "Couldn't open: "sv << path << ": "sv << strerror(errno);
      return -1;
    }

    if(drmSetClientCap(fd.el, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
      BOOST_LOG(error) << "Couldn't expose some/all drm planes"sv;
      return -1;
    }

    plane_res_t planes = drmModeGetPlaneResources(fd.el);
    if(!planes) {
      BOOST_LOG(error) << "Couldn't get drm plane resources"sv;
      return -1;
    }

    int monitor_index = 0;
    int monitor       = 0;

    BOOST_LOG(info) << "Found "sv << planes->count_planes << " planes"sv;

    int pitch;
    for(std::uint32_t x = 0; x < planes->count_planes; ++x) {
      plane_t plane = drmModeGetPlane(fd.el, planes->planes[x]);

      if(!plane) {
        BOOST_LOG(error) << "Couldn't get drm plane ["sv << x << "]: "sv << strerror(errno);
        continue;
      }

      if(!plane->fb_id) {
        continue;
      }

      fb_t fb = drmModeGetFB(fd.el, plane->fb_id);
      if(!fb) {
        BOOST_LOG(error) << "Couldn't get drm fb for plane ["sv << plane->fb_id << "]: "sv << strerror(errno);
        continue;
      }

      if(monitor++ != monitor_index) {
        continue;
      }

      if(!fb->handle) {
        BOOST_LOG(error)
          << "Couldn't get handle for Framebuffer ["sv << plane->fb_id << "]: Possibly not permitted: do [sudo setcap cap_sys_admin+ep sunshine]"sv;
        continue;
      }

      BOOST_LOG(info) << "Opened Framebuffer for plane ["sv << plane->fb_id << ']';

      auto status = drmPrimeHandleToFD(fd.el, fb->handle, 0 /* flags */, &fb_fd.el);
      if(status || fb_fd.el < 0) {
        BOOST_LOG(error) << "Couldn't get primary file descriptor for Framebuffer ["sv << fb->fb_id << "]: "sv << strerror(errno);
        continue;
      }

      BOOST_LOG(info)
        << "x("sv << plane->x << ") y("sv << plane->y << ") crtc_x("sv << plane->crtc_x << ") crtc_y("sv << plane->crtc_y << ')';

      BOOST_LOG(info)
        << "Resolution: "sv << fb->width << 'x' << fb->height
        << ": Pitch: "sv << fb->pitch
        << ": bpp: "sv << fb->bpp
        << ": depth: "sv << fb->depth;

      std::for_each_n(plane->formats, plane->count_formats, [](auto format) {
        BOOST_LOG(info) << "Format "sv << util::view(format);
      });

      width      = fb->width;
      height     = fb->height;
      pitch      = fb->pitch;
      env_width  = width;
      env_height = height;
    }

    gbm.reset(gbm::create_device(fd.el));
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

  egl::file_t fd;
  egl::file_t fb_fd;

  gbm::gbm_t gbm;
  egl::display_t display;
  egl::ctx_t ctx;

  egl::rgb_t rgb;
};
} // namespace kms

std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, int framerate) {
  auto disp = std::make_shared<kms::display_t>();

  if(disp->init(display_name, framerate)) {
    return nullptr;
  }

  BOOST_LOG(info) << "Opened DRM Display"sv;
  return disp;
}

// A list of names of displays accepted as display_name
std::vector<std::string> display_names() {
  return {};
}

} // namespace platf