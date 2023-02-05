#include "src/platform/common.h"

#include "src/main.h"
#include "src/video.h"
#include "vaapi.h"
#include "wayland.h"

using namespace std::literals;
namespace wl {
static int env_width;
static int env_height;

struct img_t : public platf::img_t {
  ~img_t() override {
    delete[] data;
    data = nullptr;
  }
};

class wlr_t : public platf::display_t {
public:
  int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
    delay    = std::chrono::nanoseconds { 1s } / config.framerate;
    mem_type = hwdevice_type;

    if(display.init()) {
      return -1;
    }

    interface.listen(display.registry());

    display.roundtrip();

    if(!interface[wl::interface_t::XDG_OUTPUT]) {
      BOOST_LOG(error) << "Missing Wayland wire for xdg_output"sv;
      return -1;
    }

    if(!interface[wl::interface_t::WLR_EXPORT_DMABUF]) {
      BOOST_LOG(error) << "Missing Wayland wire for wlr-export-dmabuf"sv;
      return -1;
    }

    auto monitor = interface.monitors[0].get();

    if(!display_name.empty()) {
      auto streamedMonitor = util::from_view(display_name);

      if(streamedMonitor >= 0 && streamedMonitor < interface.monitors.size()) {
        monitor = interface.monitors[streamedMonitor].get();
      }
    }

    monitor->listen(interface.output_manager);

    display.roundtrip();

    output = monitor->output;

    offset_x = monitor->viewport.offset_x;
    offset_y = monitor->viewport.offset_y;
    width    = monitor->viewport.width;
    height   = monitor->viewport.height;

    this->env_width  = ::wl::env_width;
    this->env_height = ::wl::env_height;

    BOOST_LOG(info) << "Selected monitor ["sv << monitor->description << "] for streaming"sv;
    BOOST_LOG(debug) << "Offset: "sv << offset_x << 'x' << offset_y;
    BOOST_LOG(debug) << "Resolution: "sv << width << 'x' << height;
    BOOST_LOG(debug) << "Desktop Resolution: "sv << env_width << 'x' << env_height;

    return 0;
  }

  int dummy_img(platf::img_t *img) override {
    return 0;
  }

  inline platf::capture_e snapshot(platf::img_t *img_out_base, std::chrono::milliseconds timeout, bool cursor) {
    auto to = std::chrono::steady_clock::now() + timeout;

    dmabuf.listen(interface.dmabuf_manager, output, cursor);
    do {
      display.roundtrip();

      if(to < std::chrono::steady_clock::now()) {
        return platf::capture_e::timeout;
      }
    } while(dmabuf.status == dmabuf_t::WAITING);

    auto current_frame = dmabuf.current_frame;

    if(
      dmabuf.status == dmabuf_t::REINIT ||
      current_frame->sd.width != width ||
      current_frame->sd.height != height) {

      return platf::capture_e::reinit;
    }

    return platf::capture_e::ok;
  }

  platf::mem_type_e mem_type;

  std::chrono::nanoseconds delay;

  wl::display_t display;
  interface_t interface;
  dmabuf_t dmabuf;

  wl_output *output;
};

class wlr_ram_t : public wlr_t {
public:
  platf::capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<platf::img_t> img, bool *cursor) override {
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

    return platf::capture_e::ok;
  }

  platf::capture_e snapshot(platf::img_t *img_out_base, std::chrono::milliseconds timeout, bool cursor) {
    auto status = wlr_t::snapshot(img_out_base, timeout, cursor);
    if(status != platf::capture_e::ok) {
      return status;
    }

    auto current_frame = dmabuf.current_frame;

    auto rgb_opt = egl::import_source(egl_display.get(), current_frame->sd);

    if(!rgb_opt) {
      return platf::capture_e::reinit;
    }

    gl::ctx.BindTexture(GL_TEXTURE_2D, (*rgb_opt)->tex[0]);

    int w, h;
    gl::ctx.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    gl::ctx.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    BOOST_LOG(debug) << "width and height: w "sv << w << " h "sv << h;

    gl::ctx.GetTextureSubImage((*rgb_opt)->tex[0], 0, 0, 0, 0, width, height, 1, GL_BGRA, GL_UNSIGNED_BYTE, img_out_base->height * img_out_base->row_pitch, img_out_base->data);
    gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

    return platf::capture_e::ok;
  }

  int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
    if(wlr_t::init(hwdevice_type, display_name, config)) {
      return -1;
    }

    egl_display = egl::make_display(display.get());
    if(!egl_display) {
      return -1;
    }

    auto ctx_opt = egl::make_ctx(egl_display.get());
    if(!ctx_opt) {
      return -1;
    }

    ctx = std::move(*ctx_opt);

    return 0;
  }

  std::shared_ptr<platf::hwdevice_t> make_hwdevice(platf::pix_fmt_e pix_fmt) override {
    if(mem_type == platf::mem_type_e::vaapi) {
      return va::make_hwdevice(width, height, false);
    }

    return std::make_shared<platf::hwdevice_t>();
  }

  std::shared_ptr<platf::img_t> alloc_img() override {
    auto img         = std::make_shared<img_t>();
    img->width       = width;
    img->height      = height;
    img->pixel_pitch = 4;
    img->row_pitch   = img->pixel_pitch * width;
    img->data        = new std::uint8_t[height * img->row_pitch];

    return img;
  }

  egl::display_t egl_display;
  egl::ctx_t ctx;
};

class wlr_vram_t : public wlr_t {
public:
  platf::capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<platf::img_t> img, bool *cursor) override {
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

    return platf::capture_e::ok;
  }

  platf::capture_e snapshot(platf::img_t *img_out_base, std::chrono::milliseconds timeout, bool cursor) {
    auto status = wlr_t::snapshot(img_out_base, timeout, cursor);
    if(status != platf::capture_e::ok) {
      return status;
    }

    auto img = (egl::img_descriptor_t *)img_out_base;
    img->reset();

    auto current_frame = dmabuf.current_frame;

    ++sequence;
    img->sequence = sequence;

    img->sd = current_frame->sd;

    // Prevent dmabuf from closing the file descriptors.
    std::fill_n(current_frame->sd.fds, 4, -1);

    return platf::capture_e::ok;
  }

  std::shared_ptr<platf::img_t> alloc_img() override {
    auto img = std::make_shared<egl::img_descriptor_t>();

    img->sequence = 0;
    img->serial   = std::numeric_limits<decltype(img->serial)>::max();
    img->data     = nullptr;

    // File descriptors aren't open
    std::fill_n(img->sd.fds, 4, -1);

    return img;
  }

  std::shared_ptr<platf::hwdevice_t> make_hwdevice(platf::pix_fmt_e pix_fmt) override {
    if(mem_type == platf::mem_type_e::vaapi) {
      return va::make_hwdevice(width, height, 0, 0, true);
    }

    return std::make_shared<platf::hwdevice_t>();
  }

  int dummy_img(platf::img_t *img) override {
    return snapshot(img, 1000ms, false) != platf::capture_e::ok;
  }

  std::uint64_t sequence {};
};

} // namespace wl

namespace platf {
std::shared_ptr<display_t> wl_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
  if(hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::vaapi && hwdevice_type != platf::mem_type_e::cuda) {
    BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
    return nullptr;
  }

  if(hwdevice_type == platf::mem_type_e::vaapi) {
    auto wlr = std::make_shared<wl::wlr_vram_t>();
    if(wlr->init(hwdevice_type, display_name, config)) {
      return nullptr;
    }

    return wlr;
  }

  auto wlr = std::make_shared<wl::wlr_ram_t>();
  if(wlr->init(hwdevice_type, display_name, config)) {
    return nullptr;
  }

  return wlr;
}

std::vector<std::string> wl_display_names() {
  std::vector<std::string> display_names;

  wl::display_t display;
  if(display.init()) {
    return {};
  }

  wl::interface_t interface;
  interface.listen(display.registry());

  display.roundtrip();

  if(!interface[wl::interface_t::XDG_OUTPUT]) {
    BOOST_LOG(warning) << "Missing Wayland wire for xdg_output"sv;
    return {};
  }

  if(!interface[wl::interface_t::WLR_EXPORT_DMABUF]) {
    BOOST_LOG(warning) << "Missing Wayland wire for wlr-export-dmabuf"sv;
    return {};
  }

  wl::env_width  = 0;
  wl::env_height = 0;

  for(auto &monitor : interface.monitors) {
    monitor->listen(interface.output_manager);
  }

  display.roundtrip();

  for(int x = 0; x < interface.monitors.size(); ++x) {
    auto monitor = interface.monitors[x].get();

    wl::env_width  = std::max(wl::env_width, (int)(monitor->viewport.offset_x + monitor->viewport.width));
    wl::env_height = std::max(wl::env_height, (int)(monitor->viewport.offset_y + monitor->viewport.height));

    display_names.emplace_back(std::to_string(x));
  }

  return display_names;
}

} // namespace platf
