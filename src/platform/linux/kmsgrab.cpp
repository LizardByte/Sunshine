#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/capability.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <filesystem>

#include "src/main.h"
#include "src/platform/common.h"
#include "src/round_robin.h"
#include "src/utility.h"
#include "src/video.h"

// Cursor rendering support through x11
#include "graphics.h"
#include "vaapi.h"
#include "wayland.h"
#include "x11grab.h"

using namespace std::literals;
namespace fs = std::filesystem;

namespace platf {

namespace kms {

class cap_sys_admin {
public:
  cap_sys_admin() {
    caps = cap_get_proc();

    cap_value_t sys_admin = CAP_SYS_ADMIN;
    if(cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_admin, CAP_SET) || cap_set_proc(caps)) {
      BOOST_LOG(error) << "Failed to gain CAP_SYS_ADMIN";
    }
  }

  ~cap_sys_admin() {
    cap_value_t sys_admin = CAP_SYS_ADMIN;
    if(cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_admin, CAP_CLEAR) || cap_set_proc(caps)) {
      BOOST_LOG(error) << "Failed to drop CAP_SYS_ADMIN";
    }
    cap_free(caps);
  }

  cap_t caps;
};

class wrapper_fb {
public:
  wrapper_fb(drmModeFB *fb)
      : fb { fb }, fb_id { fb->fb_id }, width { fb->width }, height { fb->height } {
    pixel_format = DRM_FORMAT_XRGB8888;
    modifier     = DRM_FORMAT_MOD_INVALID;
    std::fill_n(handles, 4, 0);
    std::fill_n(pitches, 4, 0);
    std::fill_n(offsets, 4, 0);
    handles[0] = fb->handle;
    pitches[0] = fb->pitch;
  }

  wrapper_fb(drmModeFB2 *fb2)
      : fb2 { fb2 }, fb_id { fb2->fb_id }, width { fb2->width }, height { fb2->height } {
    pixel_format = fb2->pixel_format;
    modifier     = (fb2->flags & DRM_MODE_FB_MODIFIERS) ? fb2->modifier : DRM_FORMAT_MOD_INVALID;

    memcpy(handles, fb2->handles, sizeof(handles));
    memcpy(pitches, fb2->pitches, sizeof(pitches));
    memcpy(offsets, fb2->offsets, sizeof(offsets));
  }

  ~wrapper_fb() {
    if(fb) {
      drmModeFreeFB(fb);
    }
    else if(fb2) {
      drmModeFreeFB2(fb2);
    }
  }

  drmModeFB *fb   = nullptr;
  drmModeFB2 *fb2 = nullptr;
  uint32_t fb_id;
  uint32_t width;
  uint32_t height;
  uint32_t pixel_format;
  uint64_t modifier;
  uint32_t handles[4];
  uint32_t pitches[4];
  uint32_t offsets[4];
};

using plane_res_t = util::safe_ptr<drmModePlaneRes, drmModeFreePlaneResources>;
using encoder_t   = util::safe_ptr<drmModeEncoder, drmModeFreeEncoder>;
using res_t       = util::safe_ptr<drmModeRes, drmModeFreeResources>;
using plane_t     = util::safe_ptr<drmModePlane, drmModeFreePlane>;
using fb_t        = std::unique_ptr<wrapper_fb>;
using crtc_t      = util::safe_ptr<drmModeCrtc, drmModeFreeCrtc>;
using obj_prop_t  = util::safe_ptr<drmModeObjectProperties, drmModeFreeObjectProperties>;
using prop_t      = util::safe_ptr<drmModePropertyRes, drmModeFreeProperty>;

using conn_type_count_t = std::map<std::uint32_t, std::uint32_t>;

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

struct connector_t {
  // For example: HDMI-A or HDMI
  std::uint32_t type;

  // Equals zero if not applicable
  std::uint32_t crtc_id;

  // For example HDMI-A-{index} or HDMI-{index}
  std::uint32_t index;

  bool connected;
};

struct monitor_t {
  std::uint32_t type;

  std::uint32_t index;

  platf::touch_port_t viewport;
};

struct card_descriptor_t {
  std::string path;

  std::map<std::uint32_t, monitor_t> crtc_to_monitor;
};

static std::vector<card_descriptor_t> card_descriptors;

static std::uint32_t from_view(const std::string_view &string) {
#define _CONVERT(x, y) \
  if(string == x) return DRM_MODE_CONNECTOR_##y

  _CONVERT("VGA"sv, VGA);
  _CONVERT("DVI-I"sv, DVII);
  _CONVERT("DVI-D"sv, DVID);
  _CONVERT("DVI-A"sv, DVIA);
  _CONVERT("S-Video"sv, SVIDEO);
  _CONVERT("LVDS"sv, LVDS);
  _CONVERT("DIN"sv, 9PinDIN);
  _CONVERT("DisplayPort"sv, DisplayPort);
  _CONVERT("DP"sv, DisplayPort);
  _CONVERT("HDMI-A"sv, HDMIA);
  _CONVERT("HDMI"sv, HDMIA);
  _CONVERT("HDMI-B"sv, HDMIB);
  _CONVERT("eDP"sv, eDP);
  _CONVERT("DSI"sv, DSI);

  BOOST_LOG(error) << "Unknown Monitor connector type ["sv << string << "]: Please report this to the GitHub issue tracker"sv;
  return DRM_MODE_CONNECTOR_Unknown;
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
  using connector_interal_t = util::safe_ptr<drmModeConnector, drmModeFreeConnector>;

  int init(const char *path) {
    cap_sys_admin admin;
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
    cap_sys_admin admin;

    auto fb2 = drmModeGetFB2(fd.el, plane->fb_id);
    if(fb2) {
      return std::make_unique<wrapper_fb>(fb2);
    }

    auto fb = drmModeGetFB(fd.el, plane->fb_id);
    if(fb) {
      return std::make_unique<wrapper_fb>(fb);
    }

    return nullptr;
  }

  crtc_t crtc(std::uint32_t id) {
    return drmModeGetCrtc(fd.el, id);
  }

  encoder_t encoder(std::uint32_t id) {
    return drmModeGetEncoder(fd.el, id);
  }

  res_t res() {
    return drmModeGetResources(fd.el);
  }

  bool is_cursor(std::uint32_t plane_id) {
    auto props = plane_props(plane_id);
    for(auto &[prop, val] : props) {
      if(prop->name == "type"sv) {
        if(val == DRM_PLANE_TYPE_CURSOR) {
          return true;
        }
        else {
          return false;
        }
      }
    }

    return false;
  }

  std::uint32_t get_panel_orientation(std::uint32_t plane_id) {
    auto props = plane_props(plane_id);
    for(auto &[prop, val] : props) {
      if(prop->name == "rotation"sv) {
        return val;
      }
    }

    BOOST_LOG(error) << "Failed to determine panel orientation, defaulting to landscape.";
    return DRM_MODE_ROTATE_0;
  }

  connector_interal_t connector(std::uint32_t id) {
    return drmModeGetConnector(fd.el, id);
  }

  std::vector<connector_t> monitors(conn_type_count_t &conn_type_count) {
    auto resources = res();
    if(!resources) {
      BOOST_LOG(error) << "Couldn't get connector resources"sv;
      return {};
    }

    std::vector<connector_t> monitors;
    std::for_each_n(resources->connectors, resources->count_connectors, [this, &conn_type_count, &monitors](std::uint32_t id) {
      auto conn = connector(id);

      std::uint32_t crtc_id = 0;

      if(conn->encoder_id) {
        auto enc = encoder(conn->encoder_id);
        if(enc) {
          crtc_id = enc->crtc_id;
        }
      }

      auto index = ++conn_type_count[conn->connector_type];

      monitors.emplace_back(connector_t {
        conn->connector_type,
        crtc_id,
        index,
        conn->connection == DRM_MODE_CONNECTED,
      });
    });

    return monitors;
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


  file_t fd;
  plane_res_t plane_res;
};

std::map<std::uint32_t, monitor_t> map_crtc_to_monitor(const std::vector<connector_t> &connectors) {
  std::map<std::uint32_t, monitor_t> result;

  for(auto &connector : connectors) {
    result.emplace(connector.crtc_id,
      monitor_t {
        connector.type,
        connector.index,
      });
  }

  return result;
}

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
    << ": Pitch: "sv << fb->pitches[0]
    << ": Offset: "sv << fb->offsets[0];

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

  int init(const std::string &display_name, const ::video::config_t &config) {
    delay = std::chrono::nanoseconds { 1s } / config.framerate;

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

      auto end = std::end(card);
      for(auto plane = std::begin(card); plane != end; ++plane) {
        if(card.is_cursor(plane->plane_id)) {
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

        if(!fb->handles[0]) {
          BOOST_LOG(error)
            << "Couldn't get handle for DRM Framebuffer ["sv << plane->fb_id << "]: Possibly not permitted: do [sudo setcap cap_sys_admin+p sunshine]"sv;
          return -1;
        }

        for(int i = 0; i < 4; ++i) {
          if(!fb->handles[i]) {
            break;
          }

          auto fb_fd = card.handleFD(fb->handles[i]);
          if(fb_fd.el < 0) {
            BOOST_LOG(error) << "Couldn't get primary file descriptor for Framebuffer ["sv << fb->fb_id << "]: "sv << strerror(errno);
            continue;
          }
        }

        BOOST_LOG(info) << "Found monitor for DRM screencasting"sv;

        // We need to find the correct /dev/dri/card{nr} to correlate the crtc_id with the monitor descriptor
        auto pos = std::find_if(std::begin(card_descriptors), std::end(card_descriptors), [&](card_descriptor_t &cd) {
          return cd.path == filestring;
        });

        if(pos == std::end(card_descriptors)) {
          // This code path shouldn't happend, but it's there just in case.
          // card_descriptors is part of the guesswork after all.
          BOOST_LOG(error) << "Couldn't find ["sv << entry.path() << "]: This shouldn't have happened :/"sv;
          return -1;
        }

        //TODO: surf_sd = fb->to_sd();

        auto crct = card.crtc(plane->crtc_id);
        kms::print(plane.get(), fb.get(), crct.get());

        img_width    = fb->width;
        img_height   = fb->height;
        img_offset_x = crct->x;
        img_offset_y = crct->y;

        this->env_width  = ::platf::kms::env_width;
        this->env_height = ::platf::kms::env_height;

        auto monitor = pos->crtc_to_monitor.find(plane->crtc_id);
        if(monitor != std::end(pos->crtc_to_monitor)) {
          auto &viewport = monitor->second.viewport;

          width  = viewport.width;
          height = viewport.height;

          switch(card.get_panel_orientation(plane->plane_id)) {
          case DRM_MODE_ROTATE_270:
            BOOST_LOG(debug) << "Detected panel orientation at 90, swapping width and height.";
            width  = viewport.height;
            height = viewport.width;
            break;
          case DRM_MODE_ROTATE_90:
          case DRM_MODE_ROTATE_180:
            BOOST_LOG(warning) << "Panel orientation is unsupported, screen capture may not work correctly.";
            break;
          }

          offset_x = viewport.offset_x;
          offset_y = viewport.offset_y;
        }

        // This code path shouldn't happend, but it's there just in case.
        // crtc_to_monitor is part of the guesswork after all.
        else {
          BOOST_LOG(warning) << "Couldn't find crtc_id, this shouldn't have happened :\\"sv;
          width    = crct->width;
          height   = crct->height;
          offset_x = crct->x;
          offset_y = crct->y;
        }

        this->card = std::move(card);

        plane_id = plane->plane_id;

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

    return 0;
  }

  inline capture_e refresh(file_t *file, egl::surface_descriptor_t *sd) {
    plane_t plane = drmModeGetPlane(card.fd.el, plane_id);

    auto fb = card.fb(plane.get());
    if(!fb) {
      BOOST_LOG(error) << "Couldn't get drm fb for plane ["sv << plane->fb_id << "]: "sv << strerror(errno);
      return capture_e::error;
    }

    if(!fb->handles[0]) {
      BOOST_LOG(error)
        << "Couldn't get handle for DRM Framebuffer ["sv << plane->fb_id << "]: Possibly not permitted: do [sudo setcap cap_sys_admin+p sunshine]"sv;
      return capture_e::error;
    }

    for(int y = 0; y < 4; ++y) {
      if(!fb->handles[y]) {
        // setting sd->fds[y] to a negative value indicates that sd->offsets[y] and sd->pitches[y]
        // are uninitialized and contain invalid values.
        sd->fds[y] = -1;
        // It's not clear whether there could still be valid handles left.
        // So, continue anyway.
        // TODO: Is this redundant?
        continue;
      }

      file[y] = card.handleFD(fb->handles[y]);
      if(file[y].el < 0) {
        BOOST_LOG(error) << "Couldn't get primary file descriptor for Framebuffer ["sv << fb->fb_id << "]: "sv << strerror(errno);
        return capture_e::error;
      }

      sd->fds[y]     = file[y].el;
      sd->offsets[y] = fb->offsets[y];
      sd->pitches[y] = fb->pitches[y];
    }

    sd->width    = fb->width;
    sd->height   = fb->height;
    sd->modifier = fb->modifier;
    sd->fourcc   = fb->pixel_format;

    if(
      fb->width != img_width ||
      fb->height != img_height) {
      return capture_e::reinit;
    }

    return capture_e::ok;
  }

  mem_type_e mem_type;

  std::chrono::nanoseconds delay;

  int img_width, img_height;
  int img_offset_x, img_offset_y;

  int plane_id;

  card_t card;

  std::optional<x11::cursor_t> cursor_opt;
};

class display_ram_t : public display_t {
public:
  display_ram_t(mem_type_e mem_type) : display_t(mem_type) {}

  int init(const std::string &display_name, const ::video::config_t &config) {
    if(!gbm::create_device) {
      BOOST_LOG(warning) << "libgbm not initialized"sv;
      return -1;
    }

    if(display_t::init(display_name, config)) {
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
        std::this_thread::sleep_for(1ns);
        now = std::chrono::steady_clock::now();
      }
      next_frame = now + delay;

      auto status = snapshot(img.get(), 1000ms, *cursor);
      switch(status) {
      case platf::capture_e::reinit:
      case platf::capture_e::error:
        return status;
      case platf::capture_e::timeout:
        img = snapshot_cb(img, false);
        break;
      case platf::capture_e::ok:
        img = snapshot_cb(img, true);
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
      return va::make_hwdevice(width, height, false);
    }

    return std::make_shared<hwdevice_t>();
  }

  capture_e snapshot(img_t *img_out_base, std::chrono::milliseconds timeout, bool cursor) {
    file_t fb_fd[4];

    egl::surface_descriptor_t sd;

    auto status = refresh(fb_fd, &sd);
    if(status != capture_e::ok) {
      return status;
    }

    auto rgb_opt = egl::import_source(display.get(), sd);

    if(!rgb_opt) {
      return capture_e::error;
    }

    auto &rgb = *rgb_opt;

    gl::ctx.BindTexture(GL_TEXTURE_2D, rgb->tex[0]);
    gl::ctx.GetTextureSubImage(rgb->tex[0], 0, img_offset_x, img_offset_y, 0, width, height, 1, GL_BGRA, GL_UNSIGNED_BYTE, img_out_base->height * img_out_base->row_pitch, img_out_base->data);

    if(cursor_opt && cursor) {
      cursor_opt->blend(*img_out_base, img_offset_x, img_offset_y);
    }

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

  gbm::gbm_t gbm;
  egl::display_t display;
  egl::ctx_t ctx;
};

class display_vram_t : public display_t {
public:
  display_vram_t(mem_type_e mem_type) : display_t(mem_type) {}

  std::shared_ptr<hwdevice_t> make_hwdevice(pix_fmt_e pix_fmt) override {
    if(mem_type == mem_type_e::vaapi) {
      return va::make_hwdevice(width, height, dup(card.fd.el), img_offset_x, img_offset_y, true);
    }

    BOOST_LOG(error) << "Unsupported pixel format for egl::display_vram_t: "sv << platf::from_pix_fmt(pix_fmt);
    return nullptr;
  }

  std::shared_ptr<img_t> alloc_img() override {
    auto img = std::make_shared<egl::img_descriptor_t>();

    img->serial      = std::numeric_limits<decltype(img->serial)>::max();
    img->data        = nullptr;
    img->pixel_pitch = 4;

    img->sequence = 0;
    std::fill_n(img->sd.fds, 4, -1);

    return img;
  }

  int dummy_img(platf::img_t *img) override {
    return snapshot(img, 1s, false) != capture_e::ok;
  }

  capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<img_t> img, bool *cursor) {
    auto next_frame = std::chrono::steady_clock::now();

    while(img) {
      auto now = std::chrono::steady_clock::now();

      if(next_frame > now) {
        std::this_thread::sleep_for((next_frame - now) / 3 * 2);
      }
      while(next_frame > now) {
        std::this_thread::sleep_for(1ns);
        now = std::chrono::steady_clock::now();
      }
      next_frame = now + delay;

      auto status = snapshot(img.get(), 1000ms, *cursor);
      switch(status) {
      case platf::capture_e::reinit:
      case platf::capture_e::error:
        return status;
      case platf::capture_e::timeout:
        img = snapshot_cb(img, false);
        break;
      case platf::capture_e::ok:
        img = snapshot_cb(img, true);
        break;
      default:
        BOOST_LOG(error) << "Unrecognized capture status ["sv << (int)status << ']';
        return status;
      }
    }

    return capture_e::ok;
  }

  capture_e snapshot(img_t *img_out_base, std::chrono::milliseconds /* timeout */, bool cursor) {
    file_t fb_fd[4];

    auto img = (egl::img_descriptor_t *)img_out_base;
    img->reset();

    auto status = refresh(fb_fd, &img->sd);
    if(status != capture_e::ok) {
      return status;
    }

    img->sequence = ++sequence;

    if(!cursor || !cursor_opt) {
      img_out_base->data = nullptr;

      for(auto x = 0; x < 4; ++x) {
        fb_fd[x].release();
      }
      return capture_e::ok;
    }

    cursor_opt->capture(*img);

    img->x -= offset_x;
    img->y -= offset_y;

    for(auto x = 0; x < 4; ++x) {
      fb_fd[x].release();
    }
    return capture_e::ok;
  }

  int init(const std::string &display_name, const ::video::config_t &config) {
    if(display_t::init(display_name, config)) {
      return -1;
    }

    if(!va::validate(card.fd.el)) {
      BOOST_LOG(warning) << "Monitor "sv << display_name << " doesn't support hardware encoding. Reverting back to GPU -> RAM -> GPU"sv;
      return -1;
    }

    sequence = 0;

    return 0;
  }

  std::uint64_t sequence;
};

} // namespace kms

std::shared_ptr<display_t> kms_display(mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
  if(hwdevice_type == mem_type_e::vaapi) {
    auto disp = std::make_shared<kms::display_vram_t>(hwdevice_type);

    if(!disp->init(display_name, config)) {
      return disp;
    }

    // In the case of failure, attempt the old method for VAAPI
  }

  auto disp = std::make_shared<kms::display_ram_t>(hwdevice_type);

  if(disp->init(display_name, config)) {
    return nullptr;
  }

  return disp;
}


/**
 * On Wayland, it's not possible to determine the position of the monitor on the desktop with KMS.
 * Wayland does allow applications to query attached monitors on the desktop,
 * however, the naming scheme is not standardized across implementations.
 * 
 * As a result, correlating the KMS output to the wayland outputs is guess work at best.
 * But, it's necessary for absolute mouse coordinates to work.
 * 
 * This is an ugly hack :(
 */
void correlate_to_wayland(std::vector<kms::card_descriptor_t> &cds) {
  auto monitors = wl::monitors();

  for(auto &monitor : monitors) {
    std::string_view name = monitor->name;

    BOOST_LOG(info) << name << ": "sv << monitor->description;

    // Try to convert names in the format:
    // {type}-{index}
    // {index} is n'th occurence of {type}
    auto index_begin = name.find_last_of('-');

    std::uint32_t index;
    if(index_begin == std::string_view::npos) {
      index = 1;
    }
    else {
      index = std::max<int64_t>(1, util::from_view(name.substr(index_begin + 1)));
    }

    auto type = kms::from_view(name.substr(0, index_begin));

    for(auto &card_descriptor : cds) {
      for(auto &[_, monitor_descriptor] : card_descriptor.crtc_to_monitor) {
        if(monitor_descriptor.index == index && monitor_descriptor.type == type) {
          monitor_descriptor.viewport.offset_x = monitor->viewport.offset_x;
          monitor_descriptor.viewport.offset_y = monitor->viewport.offset_y;

          // A sanity check, it's guesswork after all.
          if(
            monitor_descriptor.viewport.width != monitor->viewport.width ||
            monitor_descriptor.viewport.height != monitor->viewport.height) {
            BOOST_LOG(warning)
              << "Mismatch on expected Resolution compared to actual resolution: "sv
              << monitor_descriptor.viewport.width << 'x' << monitor_descriptor.viewport.height
              << " vs "sv
              << monitor->viewport.width << 'x' << monitor->viewport.height;
          }

          goto break_for_loop;
        }
      }
    }
  break_for_loop:

    BOOST_LOG(verbose) << "Reduced to name: "sv << name << ": "sv << index;
  }
}

// A list of names of displays accepted as display_name
std::vector<std::string> kms_display_names() {
  int count = 0;

  if(!fs::exists("/dev/dri")) {
    BOOST_LOG(warning) << "Couldn't find /dev/dri, kmsgrab won't be enabled"sv;
  }

  if(!gbm::create_device) {
    BOOST_LOG(warning) << "libgbm not initialized"sv;
    return {};
  }

  kms::conn_type_count_t conn_type_count;

  std::vector<kms::card_descriptor_t> cds;
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

    auto crtc_to_monitor = kms::map_crtc_to_monitor(card.monitors(conn_type_count));

    auto end = std::end(card);
    for(auto plane = std::begin(card); plane != end; ++plane) {
      auto fb = card.fb(plane.get());
      if(!fb) {
        BOOST_LOG(error) << "Couldn't get drm fb for plane ["sv << plane->fb_id << "]: "sv << strerror(errno);
        continue;
      }

      if(!fb->handles[0]) {
        BOOST_LOG(error)
          << "Couldn't get handle for DRM Framebuffer ["sv << plane->fb_id << "]: Possibly not permitted: do [sudo setcap cap_sys_admin+p sunshine]"sv;
        break;
      }

      if(card.is_cursor(plane->plane_id)) {
        continue;
      }

      // This appears to return the offset of the monitor
      auto crtc = card.crtc(plane->crtc_id);
      if(!crtc) {
        BOOST_LOG(error) << "Couldn't get crtc info: "sv << strerror(errno);
        return {};
      }

      auto it = crtc_to_monitor.find(plane->crtc_id);
      if(it != std::end(crtc_to_monitor)) {
        it->second.viewport = platf::touch_port_t {
          (int)crtc->x,
          (int)crtc->y,
          (int)crtc->width,
          (int)crtc->height,
        };
      }

      kms::env_width  = std::max(kms::env_width, (int)(crtc->x + crtc->width));
      kms::env_height = std::max(kms::env_height, (int)(crtc->y + crtc->height));

      kms::print(plane.get(), fb.get(), crtc.get());

      display_names.emplace_back(std::to_string(count++));
    }

    cds.emplace_back(kms::card_descriptor_t {
      std::move(file),
      std::move(crtc_to_monitor),
    });
  }

  if(!wl::init()) {
    correlate_to_wayland(cds);
  }

  // Deduce the full virtual desktop size
  kms::env_width  = 0;
  kms::env_height = 0;

  for(auto &card_descriptor : cds) {
    for(auto &[_, monitor_descriptor] : card_descriptor.crtc_to_monitor) {
      BOOST_LOG(debug) << "Monitor description"sv;
      BOOST_LOG(debug) << "Resolution: "sv << monitor_descriptor.viewport.width << 'x' << monitor_descriptor.viewport.height;
      BOOST_LOG(debug) << "Offset: "sv << monitor_descriptor.viewport.offset_x << 'x' << monitor_descriptor.viewport.offset_y;

      kms::env_width  = std::max(kms::env_width, (int)(monitor_descriptor.viewport.offset_x + monitor_descriptor.viewport.width));
      kms::env_height = std::max(kms::env_height, (int)(monitor_descriptor.viewport.offset_y + monitor_descriptor.viewport.height));
    }
  }

  BOOST_LOG(debug) << "Desktop resolution: "sv << kms::env_width << 'x' << kms::env_height;

  kms::card_descriptors = std::move(cds);

  return display_names;
}

} // namespace platf
