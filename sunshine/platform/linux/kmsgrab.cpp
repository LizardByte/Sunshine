#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <filesystem>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/round_robin.h"
#include "sunshine/utility.h"

// Cursor rendering support through x11
#include "graphics.h"
#include "vaapi.h"
#include "x11grab.h"

using namespace std::literals;
namespace fs = std::filesystem;

namespace platf {

namespace kms {
using plane_res_t = util::safe_ptr<drmModePlaneRes, drmModeFreePlaneResources>;
using plane_t     = util::safe_ptr<drmModePlane, drmModeFreePlane>;
using fb_t        = util::safe_ptr<drmModeFB, drmModeFreeFB>;
using fb2_t       = util::safe_ptr<drmModeFB2, drmModeFreeFB2>;
using crtc_t      = util::safe_ptr<drmModeCrtc, drmModeFreeCrtc>;
using obj_prop_t  = util::safe_ptr<drmModeObjectProperties, drmModeFreeObjectProperties>;
using prop_t      = util::safe_ptr<drmModePropertyRes, drmModeFreeProperty>;

static int env_width;
static int env_height;

std::string_view plane_type(std::uint64_t val) {
  switch(val) {
  case DRM_PLANE_TYPE_OVERLAY:
    return "DRM_PLANE_TYPE_OVERLAY"sv;
  case DRM_PLANE_TYPE_PRIMARY:
    return "DRM_PLANE_TYPE_PRIMARY"sv;
  case DRM_PLANE_TYPE_CURSOR:
    return "DRM_PLANE_TYPE_CURSOR"sv;
  }

  return "UNKNOWN"sv;
}

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
      BOOST_LOG(error) << "Couldn't expose some/all drm planes for card: "sv << path;
      return -1;
    }

    if(drmSetClientCap(fd.el, DRM_CLIENT_CAP_ATOMIC, 1)) {
      BOOST_LOG(warning) << "Couldn't expose some properties for card: "sv << path;
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

  fb2_t fb2(plane_t::pointer plane) {
    return drmModeGetFB2(fd.el, plane->fb_id);
  }

  crtc_t crtc(std::uint32_t id) {
    return drmModeGetCrtc(fd.el, id);
  }

  file_t handleFD(std::uint32_t handle) {
    file_t fb_fd;

    auto status = drmPrimeHandleToFD(fd.el, handle, 0 /* flags */, &fb_fd.el);
    if(status) {
      return {};
    }

    return fb_fd;
  }


  std::vector<std::pair<prop_t, std::uint64_t>> props(std::uint32_t id, std::uint32_t type) {
    obj_prop_t obj_prop = drmModeObjectGetProperties(fd.el, id, type);

    std::vector<std::pair<prop_t, std::uint64_t>> props;
    props.reserve(obj_prop->count_props);

    for(auto x = 0; x < obj_prop->count_props; ++x) {
      props.emplace_back(drmModeGetProperty(fd.el, obj_prop->props[x]), obj_prop->prop_values[x]);
    }

    return props;
  }

  std::vector<std::pair<prop_t, std::uint64_t>> plane_props(std::uint32_t id) {
    return props(id, DRM_MODE_OBJECT_PLANE);
  }

  std::vector<std::pair<prop_t, std::uint64_t>> crtc_props(std::uint32_t id) {
    return props(id, DRM_MODE_OBJECT_CRTC);
  }

  std::vector<std::pair<prop_t, std::uint64_t>> connector_props(std::uint32_t id) {
    return props(id, DRM_MODE_OBJECT_CONNECTOR);
  }

  plane_t
  operator[](std::uint32_t index) {
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


  file_t fd;
  plane_res_t plane_res;
};

struct kms_img_t : public img_t {
  ~kms_img_t() override {
    delete[] data;
    data = nullptr;
  }
};

void print(plane_t::pointer plane, fb_t::pointer fb, crtc_t::pointer crtc) {
  if(crtc) {
    BOOST_LOG(debug) << "crtc("sv << crtc->x << ", "sv << crtc->y << ')';
    BOOST_LOG(debug) << "crtc("sv << crtc->width << ", "sv << crtc->height << ')';
    BOOST_LOG(debug) << "plane->possible_crtcs == "sv << plane->possible_crtcs;
  }

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
  display_t(mem_type_e mem_type) : platf::display_t(), mem_type { mem_type } {}
  ~display_t() {
    while(!thread_pool.cancel(loop_id))
      ;
  }

  mem_type_e mem_type;

  std::chrono::nanoseconds delay;

  // Done on a seperate thread to prevent additional latency to capture code
  // This code detects if the framebuffer has been removed from KMS
  void task_loop() {
    capture_e capture = capture_e::reinit;

    std::uint32_t framebuffer_count = 0;

    auto end = std::end(card);
    for(auto plane = std::begin(card); plane != end; ++plane) {
      if(++framebuffer_count != framebuffer_index) {
        continue;
      }

      auto fb = card.fb(plane.get());
      if(!fb) {
        BOOST_LOG(error) << "Couldn't get drm fb for plane ["sv << plane->fb_id << "]: "sv << strerror(errno);
        capture = capture_e::error;
      }

      auto crct = card.crtc(plane->crtc_id);

      bool different =
        fb->width != img_width ||
        fb->height != img_height ||
        fb->pitch != pitch ||
        crct->x != offset_x ||
        crct->y != offset_y;

      if(!different) {
        capture = capture_e::ok;
        break;
      }
    }

    this->status = capture;

    loop_id = thread_pool.pushDelayed(&display_t::task_loop, 2s, this).task_id;
  }

  int init(const std::string &display_name, int framerate) {
    delay = std::chrono::nanoseconds { 1s } / framerate;

    int monitor_index = util::from_view(display_name);
    int monitor       = 0;

    fs::path card_dir { "/dev/dri"sv };
    for(auto &entry : fs::directory_iterator { card_dir }) {
      auto file = entry.path().filename();

      auto filestring = file.generic_u8string();
      if(filestring.size() < 4 || std::string_view { filestring }.substr(0, 4) != "card"sv) {
        continue;
      }

      kms::card_t card;
      if(card.init(entry.path().c_str())) {
        return {};
      }

      std::uint32_t framebuffer_index = 0;

      auto end = std::end(card);
      for(auto plane = std::begin(card); plane != end; ++plane) {
        ++framebuffer_index;

        bool cursor = false;

        auto props = card.plane_props(plane->plane_id);
        for(auto &[prop, val] : props) {
          if(prop->name == "type"sv) {
            BOOST_LOG(verbose) << prop->name << "::"sv << kms::plane_type(val);

            if(val == DRM_PLANE_TYPE_CURSOR) {
              // Don't count as a monitor when it is a cursor
              cursor = true;
              break;
            }
          }
        }

        if(cursor) {
          continue;
        }

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

        fb_fd = card.handleFD(fb->handle);
        if(fb_fd.el < 0) {
          BOOST_LOG(error) << "Couldn't get primary file descriptor for Framebuffer ["sv << fb->fb_id << "]: "sv << strerror(errno);
          continue;
        }

        BOOST_LOG(info) << "Found monitor for DRM screencasting"sv;

        auto crct = card.crtc(plane->crtc_id);
        kms::print(plane.get(), fb.get(), crct.get());

        img_width  = fb->width;
        img_height = fb->height;

        width  = crct->width;
        height = crct->height;

        pitch = fb->pitch;

        this->env_width  = ::platf::kms::env_width;
        this->env_height = ::platf::kms::env_height;

        offset_x = crct->x;
        offset_y = crct->y;

        this->card = std::move(card);

        this->framebuffer_index = framebuffer_index;

        goto break_loop;
      }
    }

  // Neatly break from nested for loop
  break_loop:
    if(monitor != monitor_index) {
      BOOST_LOG(error) << "Couldn't find monitor ["sv << monitor_index << ']';

      return -1;
    }

    cursor_opt = x11::cursor_t::make();

    status = capture_e::ok;

    thread_pool.start(1);
    loop_id = thread_pool.pushDelayed(&display_t::task_loop, 2s, this).task_id;

    return 0;
  }

  // When the framebuffer is reinitialized, this id can no longer be found
  std::uint32_t framebuffer_index;

  capture_e status;

  int img_width, img_height;
  int pitch;

  card_t card;
  file_t fb_fd;

  std::optional<x11::cursor_t> cursor_opt;

  util::TaskPool::task_id_t loop_id;
  util::ThreadPool thread_pool;
};

class display_ram_t : public display_t {
public:
  display_ram_t(mem_type_e mem_type) : display_t(mem_type) {}

  int init(const std::string &display_name, int framerate) {
    if(!gbm::create_device) {
      BOOST_LOG(warning) << "libgbm not initialized"sv;
      return -1;
    }

    if(display_t::init(display_name, framerate)) {
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
        img_width,
        img_height,
        0,
        pitch,
      });

    if(!rgb_opt) {
      return -1;
    }

    rgb = std::move(*rgb_opt);

    return 0;
  }

  capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<img_t> img, bool *cursor) override {
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

  std::shared_ptr<hwdevice_t> make_hwdevice(pix_fmt_e pix_fmt) override {
    if(mem_type == mem_type_e::vaapi) {
      return va::make_hwdevice(width, height);
    }

    return std::make_shared<hwdevice_t>();
  }

  capture_e snapshot(img_t *img_out_base, std::chrono::milliseconds timeout, bool cursor) {
    gl::ctx.BindTexture(GL_TEXTURE_2D, rgb->tex[0]);
    gl::ctx.GetTextureSubImage(rgb->tex[0], 0, offset_x, offset_y, 0, width, height, 1, GL_BGRA, GL_UNSIGNED_BYTE, img_out_base->height * img_out_base->row_pitch, img_out_base->data);

    if(cursor_opt && cursor) {
      cursor_opt->blend(*img_out_base, offset_x, offset_y);
    }

    return status;
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
    snapshot(img, 1s, false);
    return 0;
  }

  gbm::gbm_t gbm;
  egl::display_t display;
  egl::ctx_t ctx;

  egl::rgb_t rgb;
};

class display_vram_t : public display_t {
public:
  display_vram_t(mem_type_e mem_type) : display_t(mem_type) {}

  std::shared_ptr<hwdevice_t> make_hwdevice(pix_fmt_e pix_fmt) override {
    if(mem_type == mem_type_e::vaapi) {
      return va::make_hwdevice(width, height, dup(card.fd.el), offset_x, offset_y,
        {
          fb_fd.el,
          img_width,
          img_height,
          0,
          pitch,
        });
    }

    BOOST_LOG(error) << "Unsupported pixel format for egl::display_vram_t: "sv << platf::from_pix_fmt(pix_fmt);
    return nullptr;
  }

  std::shared_ptr<img_t> alloc_img() override {
    auto img = std::make_shared<egl::cursor_t>();

    img->serial      = std::numeric_limits<decltype(img->serial)>::max();
    img->data        = nullptr;
    img->pixel_pitch = 4;

    return img;
  }

  int dummy_img(platf::img_t *img) override {
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

  capture_e snapshot(img_t *img_out_base, std::chrono::milliseconds /* timeout */, bool cursor) {
    if(!cursor || !cursor_opt) {
      img_out_base->data = nullptr;
      return capture_e::ok;
    }

    auto img = (egl::cursor_t *)img_out_base;
    cursor_opt->capture(*img);

    img->x -= offset_x;
    img->y -= offset_y;

    return status;
  }

  int init(const std::string &display_name, int framerate) {
    if(display_t::init(display_name, framerate)) {
      return -1;
    }

    if(!va::validate(card.fd.el)) {
      BOOST_LOG(warning) << "Monitor "sv << display_name << " doesn't support hardware encoding. Reverting back to GPU -> RAM -> GPU"sv;
      return -1;
    }

    return 0;
  }
};
} // namespace kms

std::shared_ptr<display_t> kms_display(mem_type_e hwdevice_type, const std::string &display_name, int framerate) {
  if(hwdevice_type == mem_type_e::vaapi) {
    auto disp = std::make_shared<kms::display_vram_t>(hwdevice_type);

    if(!disp->init(display_name, framerate)) {
      return disp;
    }

    // In the case of failure, attempt the old method for VAAPI
  }

  auto disp = std::make_shared<kms::display_ram_t>(hwdevice_type);

  if(disp->init(display_name, framerate)) {
    return nullptr;
  }

  return disp;
}

// A list of names of displays accepted as display_name
std::vector<std::string> kms_display_names() {
  kms::env_width  = 0;
  kms::env_height = 0;

  int count = 0;

  if(!gbm::create_device) {
    BOOST_LOG(warning) << "libgbm not initialized"sv;
    return {};
  }

  std::vector<std::string> display_names;

  fs::path card_dir { "/dev/dri"sv };
  for(auto &entry : fs::directory_iterator { card_dir }) {
    auto file = entry.path().filename();

    auto filestring = file.generic_u8string();
    if(std::string_view { filestring }.substr(0, 4) != "card"sv) {
      continue;
    }

    kms::card_t card;
    if(card.init(entry.path().c_str())) {
      return {};
    }

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

      bool cursor = false;
      {
        BOOST_LOG(verbose) << "PLANE INFO ["sv << count << ']';
        auto props = card.plane_props(plane->plane_id);
        for(auto &[prop, val] : props) {
          if(prop->name == "type"sv) {
            BOOST_LOG(verbose) << prop->name << "::"sv << kms::plane_type(val);

            if(val == DRM_PLANE_TYPE_CURSOR) {
              cursor = true;
            }
          }
          else {
            BOOST_LOG(verbose) << prop->name << "::"sv << val;
          }
        }
      }

      {
        BOOST_LOG(verbose) << "CRTC INFO"sv;
        auto props = card.crtc_props(plane->crtc_id);
        for(auto &[prop, val] : props) {
          BOOST_LOG(verbose) << prop->name << "::"sv << val;
        }
      }

      // This appears to return the offset of the monitor
      auto crtc = card.crtc(plane->crtc_id);
      if(!crtc) {
        BOOST_LOG(error) << "Couldn't get crtc info: "sv << strerror(errno);
        return {};
      }

      kms::env_width  = std::max(kms::env_width, (int)(crtc->x + crtc->width));
      kms::env_height = std::max(kms::env_height, (int)(crtc->y + crtc->height));

      auto fb_2 = card.fb2(plane.get());
      for(int x = 0; x < 4 && fb_2->handles[x]; ++x) {
        BOOST_LOG(debug) << "handles::"sv << x << '(' << fb_2->handles[x] << ')';
        BOOST_LOG(debug) << "pixel_format::"sv << util::view(fb_2->pixel_format);
      }

      kms::print(plane.get(), fb.get(), crtc.get());

      if(!cursor) {
        display_names.emplace_back(std::to_string(count++));
      }
    }
  }

  return display_names;
}

} // namespace platf