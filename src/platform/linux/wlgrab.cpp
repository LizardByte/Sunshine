/**
 * @file src/platform/linux/wlgrab.cpp
 * @brief Definitions for wlgrab capture.
 */
// standard includes
#include <cstring>
#include <thread>

// local includes
#include "cuda.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/video.h"
#include "vaapi.h"
#include "vulkan_encode.h"
#include "wayland.h"

using namespace std::literals;

namespace wl {
  static int env_width;
  static int env_height;
  static bool force_ram_capture = false;

  struct img_t: public platf::img_t {
    ~img_t() override {
      delete[] data;
      data = nullptr;
    }
  };

  class wlr_t: public platf::display_t {
  public:
    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      // calculate frame interval we should capture at
      if (config.framerateX100 > 0) {
        AVRational fps_strict = ::video::framerateX100_to_rational(config.framerateX100);
        delay = std::chrono::nanoseconds(
          (static_cast<int64_t>(fps_strict.den) * 1'000'000'000LL) / fps_strict.num
        );
        BOOST_LOG(info) << "[wlgrab] Requested frame rate [" << fps_strict.num << "/" << fps_strict.den << ", approx. " << av_q2d(fps_strict) << " fps]";
      } else {
        delay = std::chrono::nanoseconds {1s} / config.framerate;
        BOOST_LOG(info) << "[wlgrab] Requested frame rate [" << config.framerate << "fps]";
      }

      mem_type = hwdevice_type;

      if (display.init()) {
        return -1;
      }

      interface.listen(display.registry());

      display.roundtrip();

      if (!interface[wl::interface_t::XDG_OUTPUT]) {
        BOOST_LOG(error) << "[wlgrab] Missing Wayland wire for xdg_output"sv;
        return -1;
      }

      if (!interface[wl::interface_t::WLR_EXPORT_DMABUF]) {
        BOOST_LOG(error) << "[wlgrab] Missing Wayland wire for wlr-export-dmabuf"sv;
        return -1;
      }

      auto monitor = interface.monitors[0].get();

      if (!display_name.empty()) {
        auto streamedMonitor = util::from_view(display_name);

        if (streamedMonitor >= 0 && streamedMonitor < interface.monitors.size()) {
          monitor = interface.monitors[streamedMonitor].get();
        }
      }

      monitor->listen(interface.output_manager);

      display.roundtrip();

      output = monitor->output;

      offset_x = monitor->viewport.offset_x;
      offset_y = monitor->viewport.offset_y;
      width = monitor->viewport.width;
      height = monitor->viewport.height;

      this->env_width = ::wl::env_width;
      this->env_height = ::wl::env_height;

      this->logical_width = monitor->viewport.logical_width;
      this->logical_height = monitor->viewport.logical_height;

      int desktop_logical_width = 0;
      int desktop_logical_height = 0;
      for (auto &monitor_entry : interface.monitors) {
        auto output_monitor = monitor_entry.get();
        desktop_logical_width = std::max(desktop_logical_width, output_monitor->viewport.offset_x + output_monitor->viewport.logical_width);
        desktop_logical_height = std::max(desktop_logical_height, output_monitor->viewport.offset_y + output_monitor->viewport.logical_height);
      }

      this->env_logical_width = desktop_logical_width;
      this->env_logical_height = desktop_logical_height;

      BOOST_LOG(info) << "[wlgrab] Selected monitor ["sv << monitor->description << "] for streaming"sv;
      BOOST_LOG(debug) << "[wlgrab] Offset: "sv << offset_x << 'x' << offset_y;
      BOOST_LOG(debug) << "[wlgrab] Resolution: "sv << width << 'x' << height;
      BOOST_LOG(debug) << "[wlgrab] Logical Resolution: "sv << logical_width << 'x' << logical_height;
      BOOST_LOG(debug) << "[wlgrab] Desktop Resolution: "sv << env_width << 'x' << env_height;
      BOOST_LOG(debug) << "[wlgrab] Logical Desktop Resolution: "sv << env_logical_width << 'x' << env_logical_height;

      return 0;
    }

    int dummy_img(platf::img_t *img) override {
      return 0;
    }

    inline platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor) {
      auto to = std::chrono::steady_clock::now() + timeout;

      // Dispatch events until we get a new frame or the timeout expires
      dmabuf.listen(interface.screencopy_manager, interface.dmabuf_interface, interface.shm_interface, output, cursor);
      do {
        auto remaining_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(to - std::chrono::steady_clock::now());
        if (remaining_time_ms.count() < 0 || !display.dispatch(remaining_time_ms)) {
          return platf::capture_e::timeout;
        }
      } while (dmabuf.status == dmabuf_t::WAITING);

      auto current_frame = dmabuf.current_frame;

      if (
        dmabuf.status == dmabuf_t::REINIT ||
        current_frame->sd.width != width ||
        current_frame->sd.height != height
      ) {
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

  class wlr_ram_t: public wlr_t {
  public:
    platf::capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto next_frame = std::chrono::steady_clock::now();

      sleep_overshoot_logger.reset();

      while (true) {
        auto now = std::chrono::steady_clock::now();

        if (next_frame > now) {
          std::this_thread::sleep_for(next_frame - now);
          sleep_overshoot_logger.first_point(next_frame);
          sleep_overshoot_logger.second_point_now_and_log();
        }

        next_frame += delay;
        if (next_frame < now) {  // some major slowdown happened; we couldn't keep up
          next_frame = now + delay;
        }

        std::shared_ptr<platf::img_t> img_out;
        auto status = snapshot(pull_free_image_cb, img_out, 1000ms, *cursor);
        switch (status) {
          case platf::capture_e::reinit:
          case platf::capture_e::error:
          case platf::capture_e::interrupted:
            return status;
          case platf::capture_e::timeout:
            if (!push_captured_image_cb(std::move(img_out), false)) {
              return platf::capture_e::ok;
            }
            break;
          case platf::capture_e::ok:
            if (!push_captured_image_cb(std::move(img_out), true)) {
              return platf::capture_e::ok;
            }
            break;
          default:
            BOOST_LOG(error) << "[wlgrab] Unrecognized capture status ["sv << std::to_underlying(status) << ']';
            return status;
        }
      }

      return platf::capture_e::ok;
    }

    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor) {
      auto status = wlr_t::snapshot(pull_free_image_cb, img_out, timeout, cursor);
      if (status != platf::capture_e::ok) {
        return status;
      }

      auto current_frame = dmabuf.current_frame;

      if (current_frame->is_shm) {
        // SHM: pixel data already in CPU RAM
        if (!pull_free_image_cb(img_out)) {
          return platf::capture_e::interrupted;
        }

        auto src = static_cast<const uint8_t *>(current_frame->shm_data);
        auto dst = static_cast<uint8_t *>(img_out->data);
        int shm_bpp = current_frame->shm_stride / width;  // bytes per pixel from stride

        static bool logged = false;
        if (!logged) {
          BOOST_LOG(info) << "[wlgrab] SHM frame: shm_stride="sv << current_frame->shm_stride
                          << " shm_bpp="sv << shm_bpp
                          << " img_row_pitch="sv << img_out->row_pitch
                          << " img_pixel_pitch="sv << img_out->pixel_pitch
                          << " "sv << width << "x"sv << height;
          logged = true;
        }

        if (shm_bpp == 4) {
          // XRGB8888/ARGB8888: direct copy, strides may differ
          auto copy_bytes = std::min(static_cast<uint32_t>(img_out->row_pitch), current_frame->shm_stride);
          for (int y = 0; y < height; y++) {
            std::memcpy(dst + y * img_out->row_pitch, src + y * current_frame->shm_stride, copy_bytes);
          }
        } else if (shm_bpp == 3) {
          // BGR888/RGB888: convert to BGRA8888 (add 0xFF alpha)
          for (int y = 0; y < height; y++) {
            auto row_src = src + y * current_frame->shm_stride;
            auto row_dst = dst + y * img_out->row_pitch;
            for (int x = 0; x < width; x++) {
              row_dst[x * 4 + 0] = row_src[x * 3 + 0];  // B
              row_dst[x * 4 + 1] = row_src[x * 3 + 1];  // G
              row_dst[x * 4 + 2] = row_src[x * 3 + 2];  // R
              row_dst[x * 4 + 3] = 0xFF;  // A
            }
          }
        } else {
          BOOST_LOG(error) << "[wlgrab] Unsupported SHM bytes per pixel: "sv << shm_bpp;
          return platf::capture_e::reinit;
        }

        img_out->frame_timestamp = current_frame->frame_timestamp;
        return platf::capture_e::ok;
      }

      // Existing DMA-BUF path — requires EGL
      if (!egl_display) {
        BOOST_LOG(error) << "[wlgrab] DMA-BUF frame received but EGL not available"sv;
        return platf::capture_e::reinit;
      }

      auto rgb_opt = egl::import_source(egl_display.get(), current_frame->sd);

      if (!rgb_opt) {
        return platf::capture_e::reinit;
      }

      if (!pull_free_image_cb(img_out)) {
        return platf::capture_e::interrupted;
      }

      gl::ctx.BindTexture(GL_TEXTURE_2D, (*rgb_opt)->tex[0]);

      // Don't remove these lines, see https://github.com/LizardByte/Sunshine/issues/453
      int h;
      int w;
      gl::ctx.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
      gl::ctx.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
      BOOST_LOG(debug) << "[wlgrab] width and height: w "sv << w << " h "sv << h;

      gl::ctx.GetTextureSubImage((*rgb_opt)->tex[0], 0, 0, 0, 0, width, height, 1, GL_BGRA, GL_UNSIGNED_BYTE, img_out->height * img_out->row_pitch, img_out->data);
      gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

      img_out->frame_timestamp = current_frame->frame_timestamp;

      return platf::capture_e::ok;
    }

    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      if (wlr_t::init(hwdevice_type, display_name, config)) {
        return -1;
      }

      egl_display = egl::make_display(display.get());
      if (!egl_display) {
        BOOST_LOG(warning) << "[wlgrab] EGL init failed, SHM-only mode"sv;
        // Continue — SHM path does not require EGL
        return 0;
      }

      auto ctx_opt = egl::make_ctx(egl_display.get());
      if (!ctx_opt) {
        BOOST_LOG(warning) << "[wlgrab] EGL context creation failed, SHM-only mode"sv;
        egl_display.reset();
        return 0;
      }

      ctx = std::move(*ctx_opt);

      return 0;
    }

    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e pix_fmt) override {
#ifdef SUNSHINE_BUILD_VAAPI
      if (mem_type == platf::mem_type_e::vaapi) {
        return va::make_avcodec_encode_device(width, height, false);
      }
#endif

#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) {
        return cuda::make_avcodec_encode_device(width, height, false);
      }
#endif

      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    std::shared_ptr<platf::img_t> alloc_img() override {
      auto img = std::make_shared<img_t>();
      img->width = width;
      img->height = height;
      img->pixel_pitch = 4;
      img->row_pitch = img->pixel_pitch * width;
      img->data = new std::uint8_t[height * img->row_pitch];

      return img;
    }

    egl::display_t egl_display;
    egl::ctx_t ctx;
  };

  class wlr_vram_t: public wlr_t {
  public:
    platf::capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto next_frame = std::chrono::steady_clock::now();

      sleep_overshoot_logger.reset();

      while (true) {
        auto now = std::chrono::steady_clock::now();

        if (next_frame > now) {
          std::this_thread::sleep_for(next_frame - now);
          sleep_overshoot_logger.first_point(next_frame);
          sleep_overshoot_logger.second_point_now_and_log();
        }

        next_frame += delay;
        if (next_frame < now) {  // some major slowdown happened; we couldn't keep up
          next_frame = now + delay;
        }

        std::shared_ptr<platf::img_t> img_out;
        auto status = snapshot(pull_free_image_cb, img_out, 1000ms, *cursor);
        switch (status) {
          case platf::capture_e::reinit:
          case platf::capture_e::error:
          case platf::capture_e::interrupted:
            return status;
          case platf::capture_e::timeout:
            if (!push_captured_image_cb(std::move(img_out), false)) {
              return platf::capture_e::ok;
            }
            break;
          case platf::capture_e::ok:
            if (!push_captured_image_cb(std::move(img_out), true)) {
              return platf::capture_e::ok;
            }
            break;
          default:
            BOOST_LOG(error) << "[wlgrab] Unrecognized capture status ["sv << std::to_underlying(status) << ']';
            return status;
        }
      }

      return platf::capture_e::ok;
    }

    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor) {
      auto status = wlr_t::snapshot(pull_free_image_cb, img_out, timeout, cursor);
      if (status != platf::capture_e::ok) {
        return status;
      }

      auto current_frame = dmabuf.current_frame;

      if (current_frame->is_shm) {
        // SHM frame — vram path can't consume it, force ram path on reinit
        BOOST_LOG(warning) << "[wlgrab] SHM frame in vram path, switching to RAM capture"sv;
        force_ram_capture = true;
        return platf::capture_e::reinit;
      }

      if (!pull_free_image_cb(img_out)) {
        return platf::capture_e::interrupted;
      }
      auto img = (egl::img_descriptor_t *) img_out.get();
      img->reset();

      ++sequence;
      img->sequence = sequence;

      img->sd = current_frame->sd;
      img->frame_timestamp = current_frame->frame_timestamp;
      img->y_invert = dmabuf.y_invert;

      // Prevent dmabuf from closing the file descriptors.
      std::fill_n(current_frame->sd.fds, 4, -1);

      return platf::capture_e::ok;
    }

    std::shared_ptr<platf::img_t> alloc_img() override {
      auto img = std::make_shared<egl::img_descriptor_t>();

      img->width = width;
      img->height = height;
      img->sequence = 0;
      img->serial = std::numeric_limits<decltype(img->serial)>::max();
      img->data = nullptr;

      // File descriptors aren't open
      std::fill_n(img->sd.fds, 4, -1);

      return img;
    }

    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e pix_fmt) override {
#ifdef SUNSHINE_BUILD_VAAPI
      if (mem_type == platf::mem_type_e::vaapi) {
        return va::make_avcodec_encode_device(width, height, 0, 0, true);
      }
#endif

#ifdef SUNSHINE_BUILD_VULKAN
      if (mem_type == platf::mem_type_e::vulkan) {
        return vk::make_avcodec_encode_device_vram(width, height, 0, 0);
      }
#endif

#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) {
        return cuda::make_avcodec_gl_encode_device(width, height, 0, 0);
      }
#endif

      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    int dummy_img(platf::img_t *img) override {
      // Empty images are recognized as dummies by the zero sequence number
      return 0;
    }

    std::uint64_t sequence {};
  };

}  // namespace wl

namespace platf {
  std::shared_ptr<display_t> wl_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::vaapi && hwdevice_type != platf::mem_type_e::cuda && hwdevice_type != platf::mem_type_e::vulkan) {
      BOOST_LOG(error) << "[wlgrab] Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    if ((hwdevice_type == platf::mem_type_e::vaapi || hwdevice_type == platf::mem_type_e::cuda || hwdevice_type == platf::mem_type_e::vulkan) && !wl::force_ram_capture) {
      auto wlr = std::make_shared<wl::wlr_vram_t>();
      if (wlr->init(hwdevice_type, display_name, config)) {
        return nullptr;
      }

      return wlr;
    }

    if (wl::force_ram_capture) {
      BOOST_LOG(info) << "[wlgrab] Using RAM capture path (GBM/DMA-BUF unavailable)"sv;
    }

    auto wlr = std::make_shared<wl::wlr_ram_t>();
    if (wlr->init(hwdevice_type, display_name, config)) {
      return nullptr;
    }

    return wlr;
  }

  std::vector<std::string> wl_display_names() {
    std::vector<std::string> display_names;

    wl::display_t display;
    if (display.init()) {
      return {};
    }

    wl::interface_t interface;
    interface.listen(display.registry());

    display.roundtrip();

    if (!interface[wl::interface_t::XDG_OUTPUT]) {
      BOOST_LOG(warning) << "[wlgrab] Missing Wayland wire for xdg_output"sv;
      return {};
    }

    if (!interface[wl::interface_t::WLR_EXPORT_DMABUF]) {
      BOOST_LOG(warning) << "[wlgrab] Missing Wayland wire for wlr-export-dmabuf"sv;
      return {};
    }

    wl::env_width = 0;
    wl::env_height = 0;

    for (auto &monitor : interface.monitors) {
      monitor->listen(interface.output_manager);
    }

    display.roundtrip();

    BOOST_LOG(info) << "[wlgrab] -------- Start of Wayland monitor list --------"sv;

    for (int x = 0; x < interface.monitors.size(); ++x) {
      auto monitor = interface.monitors[x].get();

      wl::env_width = std::max(wl::env_width, monitor->viewport.offset_x + monitor->viewport.width);
      wl::env_height = std::max(wl::env_height, monitor->viewport.offset_y + monitor->viewport.height);

      BOOST_LOG(info) << "[wlgrab] Monitor " << x << " is "sv << monitor->name << ": "sv << monitor->description;

      display_names.emplace_back(std::to_string(x));
    }

    BOOST_LOG(info) << "[wlgrab] --------- End of Wayland monitor list ---------"sv;

    return display_names;
  }

}  // namespace platf
