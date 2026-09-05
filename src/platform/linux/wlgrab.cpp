/**
 * @file src/platform/linux/wlgrab.cpp
 * @brief Definitions for wlgrab capture.
 */
// standard includes
#include <algorithm>
#include <array>
#include <thread>
#include <unistd.h>

// local includes
#include "cuda.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/video.h"
#include "vaapi.h"
#include "wayland.h"

using namespace std::literals;

namespace wl {
  static int env_width;
  static int env_height;

  /**
   * @brief Captured frame buffer shared between capture and encode stages.
   */
  struct img_t: public platf::img_t {
    /**
     * @brief Destroy the Wayland capture image.
     */
    ~img_t() override {
      delete[] data;
      data = nullptr;
    }
  };

  /**
   * @brief What colour an output is presenting in, as the compositor describes it.
   *
   * Sunshine only sends HDR to a client when the display it captured says it is
   * in HDR; see colorspace_from_client_config(), which falls back to Rec.709 for
   * every capture backend whose is_hdr() is false. Until now that was every
   * backend on Wayland: kmsgrab reads the connector's HDR_OUTPUT_METADATA
   * property and the PipeWire path reads the SPA colorimetry, and screencopy
   * carries neither.
   *
   * It does not have to. color-management-v1 hands the output's whole image
   * description to any client that asks, which is more than the DRM property
   * carries and is available on a virtual output as readily as on a physical
   * one. That is the point for a headless seat: there is no connector to read,
   * and there does not need to be.
   *
   * Values are kept in the protocol's own units and converted where they are
   * handed to Moonlight, so that the conversion sits next to the structure that
   * defines it rather than being spread over eleven callbacks.
   */
  struct output_color_t {
    bool known {false};  ///< True once the compositor finished describing the output.

    std::uint32_t primaries {0};  ///< wp_color_manager_v1 primaries enum.
    std::uint32_t transfer_function {0};  ///< wp_color_manager_v1 transfer function enum.

    std::array<std::int32_t, 8> target_primaries {};  ///< Mastering display, r_x r_y g_x g_y b_x b_y w_x w_y, CIE 1931 xy * 1000000.

    std::uint32_t target_min_luminance {0};  ///< cd/m² * 10000.
    std::uint32_t target_max_luminance {0};  ///< cd/m².
    std::uint32_t target_max_cll {0};  ///< cd/m².
    std::uint32_t target_max_fall {0};  ///< cd/m².
  };

  /**
   * @brief State carried through one image description query.
   */
  struct color_query_t {
    output_color_t color;  ///< Values collected from the information events.
    bool ready {false};  ///< True once the image description resolved.
    bool failed {false};  ///< True when the compositor refused to describe it.
    bool done {false};  ///< True once every information event has arrived.
  };

  static void color_desc_failed(void *data, wp_image_description_v1 *, std::uint32_t cause, const char *msg) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    auto query = (color_query_t *) data;
    query->failed = true;
    BOOST_LOG(debug) << "[wlgrab] The compositor would not describe the output's colour: "sv << (msg ? msg : "no reason given");
  }

  static void color_desc_ready(void *data, wp_image_description_v1 *, std::uint32_t identity) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    ((color_query_t *) data)->ready = true;
  }

  static void color_info_done(void *data, wp_image_description_info_v1 *) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    auto query = (color_query_t *) data;
    query->done = true;
    query->color.known = true;
  }

  static void color_info_icc_file(void *data, wp_image_description_info_v1 *, std::int32_t fd, std::uint32_t size) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    // An ICC profile describes an SDR output. Nothing to read, but the file
    // descriptor is ours now and leaking one per stream would be a slow leak
    // rather than no leak at all.
    if (fd >= 0) {
      close(fd);
    }
  }

  static void color_info_primaries(void *data, wp_image_description_info_v1 *, std::int32_t r_x, std::int32_t r_y, std::int32_t g_x, std::int32_t g_y, std::int32_t b_x, std::int32_t b_y, std::int32_t w_x, std::int32_t w_y) {  // NOSONAR: the event's shape is fixed by the Wayland C protocol - eight coordinates, the proxy and the user pointer (cpp:S107, cpp:S5008)
    // The primary colour volume, which is not what Moonlight's structure wants:
    // that is mastering display metadata, and it arrives in target_primaries.
  }

  static void color_info_primaries_named(void *data, wp_image_description_info_v1 *, std::uint32_t primaries) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    ((color_query_t *) data)->color.primaries = primaries;
  }

  static void color_info_tf_power(void *data, wp_image_description_info_v1 *, std::uint32_t eexp) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    // A power-function transfer characteristic, so not PQ and not HDR.
  }

  static void color_info_tf_named(void *data, wp_image_description_info_v1 *, std::uint32_t tf) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    ((color_query_t *) data)->color.transfer_function = tf;
  }

  static void color_info_luminances(void *data, wp_image_description_info_v1 *, std::uint32_t min_lum, std::uint32_t max_lum, std::uint32_t reference_lum) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    // The primary colour volume's luminances. target_luminance carries the ones
    // that correspond to SMPTE ST 2086 and therefore to SS_HDR_METADATA.
  }

  static void color_info_target_primaries(void *data, wp_image_description_info_v1 *, std::int32_t r_x, std::int32_t r_y, std::int32_t g_x, std::int32_t g_y, std::int32_t b_x, std::int32_t b_y, std::int32_t w_x, std::int32_t w_y) {  // NOSONAR: the event's shape is fixed by the Wayland C protocol - eight coordinates, the proxy and the user pointer (cpp:S107, cpp:S5008)
    auto &color = ((color_query_t *) data)->color;
    color.target_primaries = {r_x, r_y, g_x, g_y, b_x, b_y, w_x, w_y};
  }

  static void color_info_target_luminance(void *data, wp_image_description_info_v1 *, std::uint32_t min_lum, std::uint32_t max_lum) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    auto &color = ((color_query_t *) data)->color;
    color.target_min_luminance = min_lum;
    color.target_max_luminance = max_lum;
  }

  static void color_info_target_max_cll(void *data, wp_image_description_info_v1 *, std::uint32_t max_cll) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    ((color_query_t *) data)->color.target_max_cll = max_cll;
  }

  static void color_info_target_max_fall(void *data, wp_image_description_info_v1 *, std::uint32_t max_fall) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    ((color_query_t *) data)->color.target_max_fall = max_fall;
  }

  static const wp_image_description_v1_listener color_desc_listener = {
    .failed = color_desc_failed,
    .ready = color_desc_ready,
  };

  static const wp_image_description_info_v1_listener color_info_listener = {
    .done = color_info_done,
    .icc_file = color_info_icc_file,
    .primaries = color_info_primaries,
    .primaries_named = color_info_primaries_named,
    .tf_power = color_info_tf_power,
    .tf_named = color_info_tf_named,
    .luminances = color_info_luminances,
    .target_primaries = color_info_target_primaries,
    .target_luminance = color_info_target_luminance,
    .target_max_cll = color_info_target_max_cll,
    .target_max_fall = color_info_target_max_fall,
  };

  static void color_output_changed(void *data, wp_color_management_output_v1 *) {  // NOSONAR(cpp:S5008): the listener signature is fixed by the Wayland C protocol
    // The output's colour can change while a stream runs, when the compositor
    // is told to turn HDR on or off. Nothing is read here: Sunshine settles the
    // colourspace when the encode session is built, so a change mid stream only
    // takes effect on the next one. Present because libwayland dispatches into
    // this table and a null entry would be a crash.
  }

  static const wp_color_management_output_v1_listener color_output_listener = {
    .image_description_changed = color_output_changed,
  };

  /**
   * @brief Ask the compositor what colour an output is presenting in.
   *
   * Never fails loudly. A compositor without color-management-v1, or one that
   * refuses to describe the output, leaves the result unknown, which reads as
   * SDR everywhere it is used. That is what every wlroots compositor did before
   * this existed, so the fallback is the old behaviour rather than a new one.
   *
   * @param display Wayland connection used to round-trip.
   * @param manager color-management-v1 global, or nullptr when absent.
   * @param output The output being captured.
   * @return What the compositor said, or an unknown colour.
   */
  static output_color_t query_output_color(wl::display_t &display, wp_color_manager_v1 *manager, wl_output *output) {
    color_query_t query;

    if (!manager || !output) {
      return query.color;
    }

    auto cm_output = wp_color_manager_v1_get_output(manager, output);
    if (!cm_output) {
      return query.color;
    }

    wp_color_management_output_v1_add_listener(cm_output, &color_output_listener, &query);

    auto desc = wp_color_management_output_v1_get_image_description(cm_output);
    if (!desc) {
      wp_color_management_output_v1_destroy(cm_output);
      return query.color;
    }

    wp_image_description_v1_add_listener(desc, &color_desc_listener, &query);
    display.roundtrip();

    if (query.ready && !query.failed) {
      // The information object is destroyed by the compositor when it sends
      // done, so it must not be destroyed here as well.
      auto info = wp_image_description_v1_get_information(desc);
      if (info) {
        wp_image_description_info_v1_add_listener(info, &color_info_listener, &query);
        display.roundtrip();
      }
    }

    wp_image_description_v1_destroy(desc);
    wp_color_management_output_v1_destroy(cm_output);

    return query.color;
  }

  /**
   * @brief Wayland screencopy capture backend shared by RAM and VRAM paths.
   */
  class wlr_t: public platf::display_t {
  public:
    /**
     * @brief Initialize Wayland screencopy capture for the selected output.
     *
     * @param hwdevice_type Hardware device type requested for capture or encode.
     * @param display_name Display name.
     * @param config Configuration values to apply.
     * @return 0 on success; nonzero or negative platform status on failure.
     */
    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      // calculate frame interval we should capture at
      delay = ::video::capture_frame_interval(config);
      const AVRational fps = ::video::framerate_to_rational(config);
      if (fps.den != 1) {
        BOOST_LOG(info) << "[wlgrab] Requested frame rate [" << fps.num << "/" << fps.den << ", approx. " << av_q2d(fps) << " fps]";
      } else {
        BOOST_LOG(info) << "[wlgrab] Requested frame rate [" << fps.num << "fps]";
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

      // Populate xdg_output info (name, viewport) for every monitor up
      // front so we can match by stable name in addition to index.
      for (auto &m : interface.monitors) {
        m->listen(interface.output_manager);
      }
      display.roundtrip();

      auto monitor = interface.monitors[0].get();

      if (!display_name.empty()) {
        // Match by xdg_output name first (stable across hotplug, e.g.
        // "eDP-1", "HEADLESS-2"). Fall back to numeric index for
        // backward compatibility with existing configs.
        bool matched = false;
        for (auto &m : interface.monitors) {
          if (m->name == display_name) {
            monitor = m.get();
            matched = true;
            break;
          }
        }
        if (!matched) {
          auto streamedMonitor = util::from_view(display_name);
          if (streamedMonitor >= 0 && streamedMonitor < interface.monitors.size()) {
            monitor = interface.monitors[streamedMonitor].get();
          }
        }
      }

      output = monitor->output;

      // Read once, here, rather than per frame. Sunshine asks is_hdr() when it
      // builds an encode session, and a session is rebuilt whenever the client
      // reconnects or the capture reinitialises, which is also every moment at
      // which the seat's output could have changed colour.
      hdr_color = query_output_color(display, interface.color_manager, output);

      if (!interface[wl::interface_t::COLOR_MANAGER]) {
        BOOST_LOG(info) << "[wlgrab] The compositor has no color-management-v1, so this capture is SDR"sv;
      } else if (hdr_color.known) {
        BOOST_LOG(info) << "[wlgrab] Output colour: primaries "sv << hdr_color.primaries
                        << ", transfer function "sv << hdr_color.transfer_function
                        << (is_hdr() ? " (HDR)"sv : " (SDR)"sv);
      } else {
        BOOST_LOG(info) << "[wlgrab] The compositor did not describe the output's colour, treating it as SDR"sv;
      }

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

    /**
     * @brief Populate a fallback image when real capture data is unavailable.
     *
     * @param img Image or frame object to read from or populate.
     * @return Capture status reported to the streaming pipeline.
     */
    int dummy_img(platf::img_t *img) override {
      return 0;
    }

    /**
     * @brief Capture a display frame into the provided image object.
     *
     * @param pull_free_image_cb Callback that provides an available image buffer.
     * @param img_out Captured wlroots image returned to the streaming pipeline.
     * @param timeout Maximum time to wait for the operation.
     * @param cursor Cursor image or visibility state to composite.
     * @return Capture status reported to the streaming pipeline.
     */
    inline platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor) {
      auto to = std::chrono::steady_clock::now() + timeout;

      // Dispatch events until we get a new frame or the timeout expires
      dmabuf.listen(interface.screencopy_manager, interface.dmabuf_interface, &interface.supported_modifiers, output, cursor);
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

    /**
     * @brief Report whether the captured output is presenting in HDR.
     *
     * @return True when the output is BT.2020 with the PQ transfer function.
     */
    bool is_hdr() override {
      return hdr_color.known &&
             hdr_color.primaries == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020 &&
             hdr_color.transfer_function == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
    }

    /**
     * @brief Read HDR metadata for the captured output.
     *
     * @param metadata Output structure populated with HDR metadata.
     * @return True when HDR metadata was written to the output structure.
     */
    bool get_hdr_metadata(SS_HDR_METADATA &metadata) override {
      if (!is_hdr()) {
        return false;
      }

      metadata = {};

      // color-management-v1 carries CIE 1931 coordinates multiplied by a
      // million; Moonlight's structure, which follows CTA-861, carries them
      // multiplied by fifty thousand.
      auto coord = [](std::int32_t value) {
        return (std::uint16_t) std::clamp<std::int32_t>(value / 20, 0, 50000);
      };

      for (int i = 0; i < 3; i++) {
        metadata.displayPrimaries[i].x = coord(hdr_color.target_primaries[i * 2]);
        metadata.displayPrimaries[i].y = coord(hdr_color.target_primaries[i * 2 + 1]);
      }

      metadata.whitePoint.x = coord(hdr_color.target_primaries[6]);
      metadata.whitePoint.y = coord(hdr_color.target_primaries[7]);

      // The luminances already agree: the protocol gives the minimum in ten
      // thousandths of a nit and everything else in whole nits, which is what
      // Moonlight expects. Only the width has to be watched, because these
      // arrive as 32 bit values and are handed on as 16 bit ones.
      auto nits = [](std::uint32_t value) {
        return (std::uint16_t) std::min<std::uint32_t>(value, 65535);
      };

      metadata.minDisplayLuminance = nits(hdr_color.target_min_luminance);
      metadata.maxDisplayLuminance = nits(hdr_color.target_max_luminance);
      metadata.maxContentLightLevel = nits(hdr_color.target_max_cll);
      metadata.maxFrameAverageLightLevel = nits(hdr_color.target_max_fall);

      return true;
    }

    platf::mem_type_e mem_type;  ///< Mem type.

    std::chrono::nanoseconds delay;  ///< Delay before the timer task becomes eligible to run.

    wl::display_t display;  ///< Wayland display connection used for capture.
    interface_t interface;  ///< Wayland registry interfaces required by screencopy.
    dmabuf_t dmabuf;  ///< DMA-BUF feedback and format state advertised by the compositor.

    wl_output *output;  ///< Wayland output selected for capture.
    output_color_t hdr_color;  ///< What colour the compositor says the output is presenting in.
  };

  /**
   * @brief Wayland screencopy backend that copies frames into system memory.
   */
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

    /**
     * @brief Capture a display frame into the provided image object.
     *
     * @param pull_free_image_cb Callback that provides an available image buffer.
     * @param img_out Captured wlroots image returned to the streaming pipeline.
     * @param timeout Maximum time to wait for the operation.
     * @param cursor Cursor image or visibility state to composite.
     * @return Capture status reported to the streaming pipeline.
     */
    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor) {
      auto status = wlr_t::snapshot(pull_free_image_cb, img_out, timeout, cursor);
      if (status != platf::capture_e::ok) {
        return status;
      }

      auto current_frame = dmabuf.current_frame;

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

    /**
     * @brief Initialize Wayland capture that copies frames into system memory.
     *
     * @param hwdevice_type Hardware device type requested for capture or encode.
     * @param display_name Display name.
     * @param config Configuration values to apply.
     * @return 0 on success; nonzero or negative platform status on failure.
     */
    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      if (wlr_t::init(hwdevice_type, display_name, config)) {
        return -1;
      }

      egl_display = egl::make_display(display.get());
      if (!egl_display) {
        return -1;
      }

      auto ctx_opt = egl::make_ctx(egl_display.get());
      if (!ctx_opt) {
        return -1;
      }

      ctx = std::move(*ctx_opt);

      return 0;
    }

    /**
     * @brief Create AVCodec encode device.
     *
     * @param pix_fmt Sunshine pixel format to convert or allocate for.
     * @return Constructed AVCodec encode device object.
     */
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

    /**
     * @brief Allocate an image buffer compatible with this display backend.
     *
     * @return Allocated img object, or null when unavailable.
     */
    std::shared_ptr<platf::img_t> alloc_img() override {
      auto img = std::make_shared<img_t>();
      img->width = width;
      img->height = height;
      img->pixel_pitch = 4;
      img->row_pitch = img->pixel_pitch * width;
      img->data = new std::uint8_t[height * img->row_pitch];

      return img;
    }

    egl::display_t egl_display;  ///< EGL display.
    egl::ctx_t ctx;  ///< EGL context used for wlroots capture conversion.
  };

  /**
   * @brief Wayland screencopy backend that exports frames as GPU resources.
   */
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

    /**
     * @brief Capture a display frame into the provided image object.
     *
     * @param pull_free_image_cb Callback that provides an available image buffer.
     * @param img_out Captured wlroots image returned to the streaming pipeline.
     * @param timeout Maximum time to wait for the operation.
     * @param cursor Cursor image or visibility state to composite.
     * @return Capture status reported to the streaming pipeline.
     */
    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor) {
      auto status = wlr_t::snapshot(pull_free_image_cb, img_out, timeout, cursor);
      if (status != platf::capture_e::ok) {
        return status;
      }

      if (!pull_free_image_cb(img_out)) {
        return platf::capture_e::interrupted;
      }
      auto img = (egl::img_descriptor_t *) img_out.get();
      img->reset();

      auto current_frame = dmabuf.current_frame;

      ++sequence;
      img->sequence = sequence;

      img->sd = current_frame->sd;
      img->frame_timestamp = current_frame->frame_timestamp;

      // Prevent dmabuf from closing the file descriptors.
      std::fill_n(current_frame->sd.fds, 4, -1);

      return platf::capture_e::ok;
    }

    /**
     * @brief Allocate an image buffer compatible with this display backend.
     *
     * @return Allocated img object, or null when unavailable.
     */
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

    /**
     * @brief Create AVCodec encode device.
     *
     * @param pix_fmt Sunshine pixel format to convert or allocate for.
     * @return Constructed AVCodec encode device object.
     */
    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e pix_fmt) override {
#ifdef SUNSHINE_BUILD_VAAPI
      if (mem_type == platf::mem_type_e::vaapi) {
        return va::make_avcodec_encode_device(width, height, 0, 0, true);
      }
#endif

#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) {
        return cuda::make_avcodec_gl_encode_device(width, height, 0, 0);
      }
#endif

      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    /**
     * @brief Populate a fallback image when real capture data is unavailable.
     *
     * @param img Image or frame object to read from or populate.
     * @return Capture status reported to the streaming pipeline.
     */
    int dummy_img(platf::img_t *img) override {
      // Empty images are recognized as dummies by the zero sequence number
      return 0;
    }

    std::uint64_t sequence {};  ///< Monotonic capture sequence assigned to Wayland frames.
  };

}  // namespace wl

namespace platf {
  /**
   * @brief Create a Wayland capture backend for the requested memory type.
   */
  std::shared_ptr<display_t> wl_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::vaapi && hwdevice_type != platf::mem_type_e::cuda) {
      BOOST_LOG(error) << "[wlgrab] Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    if (hwdevice_type == platf::mem_type_e::vaapi || hwdevice_type == platf::mem_type_e::cuda) {
      auto wlr = std::make_shared<wl::wlr_vram_t>();
      if (wlr->init(hwdevice_type, display_name, config)) {
        return nullptr;
      }

      return wlr;
    }

    auto wlr = std::make_shared<wl::wlr_ram_t>();
    if (wlr->init(hwdevice_type, display_name, config)) {
      return nullptr;
    }

    return wlr;
  }

  /**
   * @brief Enumerate capture display names reported by the Wayland compositor.
   */
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

      display_names.emplace_back(monitor->name.empty() ? std::to_string(x) : monitor->name);
    }

    BOOST_LOG(info) << "[wlgrab] --------- End of Wayland monitor list ---------"sv;

    return display_names;
  }

}  // namespace platf
