/**
 * @file src/platform/macos/display.mm
 * @brief Definitions for display capture on macOS.
 */

// standard includes
#include <charconv>
#include <chrono>
#include <optional>
#include <string_view>

// local includes
#include "src/config.h"
#include "src/display_device.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/misc.h"
#include "src/platform/macos/nv12_zero_device.h"
#include "src/platform/macos/sck_video.h"
#include "cf_helpers.h"
#include "qpc_chrono.h"

// Avoid conflict between AVFoundation and libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "src/platform/macos/videotoolbox.h"
#include "src/video.h"
#undef AVMediaType

#include <chrono>
#include <cmath>
#include <limits>
#include <mach/mach_time.h>

#import <CoreVideo/CoreVideo.h>

using namespace std::chrono;

namespace platf {
  using namespace std::literals;

  namespace {
    std::optional<CGDirectDisplayID> parse_display_id(std::string_view display_name) {
      if (display_name.empty()) {
        return std::nullopt;
      }

      CGDirectDisplayID display_id {};
      const auto *const begin {display_name.data()};
      const auto *const end {display_name.data() + display_name.size()};
      const auto [ptr, ec] {std::from_chars(begin, end, display_id)};
      if (ec != std::errc {} || ptr != end) {
        return std::nullopt;
      }

      return display_id;
    }
  }  // namespace

  /**
   * @brief macOS display capture source and image buffers.
   */
  struct sck_display_t: public display_t {
    ::screen_capture *sc {};
    CGDirectDisplayID display_id {};  ///< Display ID.
    std::unique_ptr<display_device::DisplayPowerGuardInterface> display_power_guard;  ///< Display power guard.
    int client_frame_rate {};
    AVRational client_frame_rate_strict {};
    std::optional<nanoseconds> uptime_to_steady_offset;

    ~sck_display_t() override {
      if (sc != nullptr) {
        sck_video_capture_destroy(sc);
        sc = nullptr;
      }
    }

    int init(const ::video::config_t &config) {
      client_frame_rate = config.framerate;
      if (client_frame_rate <= 0) {
        client_frame_rate = 60;
      }

      if (config.framerateX100 > 0) {
        client_frame_rate_strict = ::video::framerateX100_to_rational(config.framerateX100);
        BOOST_LOG(info) << "[sck] Requested frame rate [" << client_frame_rate_strict.num << "/" << client_frame_rate_strict.den
                        << ", approx. " << av_q2d(client_frame_rate_strict) << " fps]";
      } else {
        BOOST_LOG(info) << "[sck] Requested frame rate [" << config.framerate << "fps]";
      }
      return 0;
    }

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto get_target_fps = [&]() -> AVRational {
        // Use exactly the requested rate if the client sent an X100 value
        if (client_frame_rate_strict.num > 0) {
          return client_frame_rate_strict;
        }
        // Adjust capture frame interval when display refresh rate is not integral but very close to requested fps.
        if (sc->display_refresh_rate.den > 1) {
          int display_refresh_rate_rounded = lround((double) sc->display_refresh_rate.num / sc->display_refresh_rate.den);
          AVRational candidate = sc->display_refresh_rate;
          if (client_frame_rate % display_refresh_rate_rounded == 0) {
            candidate.num *= client_frame_rate / display_refresh_rate_rounded;
          } else if (display_refresh_rate_rounded % client_frame_rate == 0) {
            candidate.den *= display_refresh_rate_rounded / client_frame_rate;
          }
          double candidate_rate = (double) candidate.num / candidate.den;
          // Can only decrease requested fps, otherwise client may start accumulating frames and suffer increased latency.
          if (client_frame_rate > candidate_rate && candidate_rate / client_frame_rate > 0.99) {
            BOOST_LOG(info) << "Adjusted capture rate to " << candidate_rate << "fps to better match display";
            return candidate;
          }
        }

        return AVRational {client_frame_rate, 1};
      };

      AVRational target_fps = get_target_fps();

      std::optional<steady_clock::time_point> pacing_anchor;  // time of output slot 0
      uint64_t emitted = 0;  // frames emitted since pacing_anchor

      // Time of the Nth output slot: anchor + N/target
      auto slot_time = [&](uint32_t n) {
        const uint64_t seconds = (uint64_t) n * target_fps.den / target_fps.num;
        const uint64_t remainder = (uint64_t) n * target_fps.den % target_fps.num;
        return *pacing_anchor +
          nanoseconds(1s) * seconds +
          nanoseconds(1s) * remainder / target_fps.num;
      };

      while (true) {
        std::shared_ptr<platf::img_t> img_out;
        platf::capture_e status = snapshot(pull_free_image_cb, img_out, 200ms, *cursor);

        switch (status) {
          case platf::capture_e::reinit:
          case platf::capture_e::error:
          case platf::capture_e::interrupted:
            return status;
          case platf::capture_e::timeout:
            // No new frame (e.g. static screen). Push a heartbeat (frame_captured = false) so
            // display-switch detection and shutdown still run, then keep waiting.
            if (!push_captured_image_cb(std::move(img_out), false)) {
              return capture_e::ok;
            }
            continue;
          case platf::capture_e::ok:
            break;
          default:
            BOOST_LOG(error) << "Unrecognized capture status ["sv << (int) status << ']';
            return status;
        }

        if (!img_out) {
          continue;
        }

        const auto cpt = *img_out->capture_pacing_timestamp;
        if (!pacing_anchor || cpt < *pacing_anchor) {
          pacing_anchor = cpt;
          emitted = 0;
        }

        auto st = slot_time(emitted);
        auto st_next = slot_time(emitted + 1);

        if (cpt < st) {
          // Capture is faster than stream, drop this frame
          continue;
        }

        if (cpt < st_next) {
          // Capture is within the expected slot, advance slot counter
          ++emitted;
        } else {
          // Capture is late/slow, restart slot counter
          pacing_anchor = cpt;
          emitted = 1;
        }

        if (!push_captured_image_cb(std::move(img_out), true)) {
          return capture_e::ok;
        }
      }
    }

    // Convert a mach absolute time tick count (e.g. SCStreamFrameInfoDisplayTime) to nanoseconds
    static uint64_t mach_ticks_to_ns(uint64_t mach_ticks) {
      static const mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t {};
        mach_timebase_info(&t);
        return t;
      }();
      return static_cast<uint64_t>(
        static_cast<__int128>(mach_ticks) * tb.numer / tb.denom
      );
    }

    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, milliseconds timeout, bool cursor) {
      @autoreleasepool {
        // Changes to cursor status will take effect on future frames
        sck_set_show_cursor(sc, cursor);

        CMSampleBufferRef sb = sck_get_latest_sample_buffer(sc, timeout);

        if (sc->capture_failed) {
          return capture_e::error;
        }

        if (sb == nullptr) {
          return capture_e::timeout;
        }

        CVImageBufferRef image_buffer = CMSampleBufferGetImageBuffer(sb);
        if (image_buffer == nullptr) {
          return capture_e::timeout;
        }

        //BOOST_LOG(debug) << cf_desc_to_std_string(sb);

        // This is the timestamp of the vsync when the captured frame was or will be displayed.
        // It's possible for it to be delivered to this callback before being displayed.
        auto present_timestamp = steady_clock::now();

        int64_t display_ticks = 0;
        CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sb, false);
        if (attachments && CFArrayGetCount(attachments) > 0) {
          auto attachment = (CFDictionaryRef) CFArrayGetValueAtIndex(attachments, 0);
          CFTypeRef cf_display_time = CFDictionaryGetValue(attachment, SCStreamFrameInfoDisplayTime);
          if (cf_display_time != NULL) {
            CFNumberGetValue((CFNumberRef) cf_display_time, kCFNumberSInt64Type, &display_ticks);
            const int64_t delta_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - mach_ticks_to_ns(display_ticks);
            present_timestamp -= duration_cast<steady_clock::duration>(nanoseconds(delta_ns));
          }
        }

        if (!display_ticks) {
          // fallback to sample buffer's pts, if this fails we'll at least have now()
          const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sb);
          if (CMTIME_IS_VALID(pts)) {
            const CMTime host_now = CMClockGetTime(CMClockGetHostTimeClock());
            const double delta_sec = CMTimeGetSeconds(CMTimeSubtract(host_now, pts));
            present_timestamp -= duration_cast<steady_clock::duration>(duration<double>(delta_sec));
          }
        }

        auto new_sample_buffer = std::make_shared<av_sample_buf_t>(sb);
        auto new_pixel_buffer = std::make_shared<av_pixel_buf_t>(new_sample_buffer->buf);

        if (!pull_free_image_cb(img_out)) {
          return capture_e::interrupted;
        }
        auto av_img = std::static_pointer_cast<av_img_t>(img_out);

        av_img->sample_buffer = new_sample_buffer;
        av_img->pixel_buffer = new_pixel_buffer;
        img_out->data = new_pixel_buffer->data();

        if (new_pixel_buffer->usingSoftwareEncode) {
          img_out->width = (int) CVPixelBufferGetWidth(new_pixel_buffer->buf);
          img_out->height = (int) CVPixelBufferGetHeight(new_pixel_buffer->buf);
          img_out->row_pitch = (int) CVPixelBufferGetBytesPerRow(new_pixel_buffer->buf);
          img_out->pixel_pitch = img_out->width ? img_out->row_pitch / img_out->width : 0;
        }

        img_out->capture_pacing_timestamp = present_timestamp; // used for pacing & pts
        img_out->frame_timestamp = present_timestamp;          // used for host latency stat, maybe should be now()

        return capture_e::ok;
      }
    }

    /**
     * @brief Allocate an image buffer compatible with this display backend.
     *
     * @return Allocated img object, or null when unavailable.
     */
    std::shared_ptr<img_t> alloc_img() override {
      return std::make_shared<av_img_t>();
    }

    static void setResolution(::screen_capture *sc, int width, int height) {
      if (sc->stream_properties != nil) {
        sck_set_frame_size(sc, width, height);
      }
    }

    static void setPixelFormat(::screen_capture *sc, OSType pixelFormat) {
      if (sc->stream_properties != nil) {
        if (pixelFormat != sc->stream_properties.pixelFormat) {
          BOOST_LOG(info) << "setPixelFormat "sv << pixelFormat;
          [sc->stream_properties setPixelFormat:pixelFormat];
        }
      }
    }

    std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e pix_fmt) override {
      BOOST_LOG(debug) << "make_avcodec_encode_device pix_fmt " << (int) pix_fmt;

      if (pix_fmt == pix_fmt_e::yuv420p) {
        setPixelFormat(sc, kCVPixelFormatType_32BGRA);

        return std::make_unique<avcodec_encode_device_t>();
      } else {
        auto device = std::make_unique<nv12_zero_device>();

        device->init(sc, pix_fmt, sc->colorspace.full_range, setResolution, setPixelFormat);

        return device;
      }
    }

    std::unique_ptr<videotoolbox_encode_device_t> make_videotoolbox_encode_device(pix_fmt_e pix_fmt) override {
      BOOST_LOG(debug) << "make_videotoolbox_encode_device pix_fmt " << (int) pix_fmt;

      auto device = std::make_unique<videotoolbox_encode_device_t>();
      device->init(sc, pix_fmt, sc->colorspace.full_range, setResolution, setPixelFormat);
      return device;
    }

    int dummy_img(img_t *img) override {
      if (!platf::is_screen_capture_allowed()) {
        // Capture cannot succeed without the screen capture permission.
        // A non-zero return value indicates failure to the calling function.
        return 1;
      }

      if (!img) {
        return -1;
      }

      auto pull_dummy_img_callback = [&img](std::shared_ptr<platf::img_t> &img_out) -> bool {
        img_out = img->shared_from_this();
        return true;
      };

      std::shared_ptr<platf::img_t> img_out;
      return snapshot(pull_dummy_img_callback, img_out, 1000ms, true) == capture_e::ok ? 0 : -1;
    }

    bool is_hdr() override {
      // true when potential EDR > 1.0
      for (NSScreen *screen in NSScreen.screens) {
        NSNumber *screen_num = screen.deviceDescription[@"NSScreenNumber"];
        if (sc->display_id == (CGDirectDisplayID) screen_num.intValue) {
          return screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
        }
      }
      return false;
    }

    bool get_hdr_metadata(SS_HDR_METADATA &metadata) override {
      // Report Rec 2020 primaries
      metadata.displayPrimaries[0].x = 0.708f * 50000;
      metadata.displayPrimaries[0].y = 0.292f * 50000;
      metadata.displayPrimaries[1].x = 0.170f * 50000;
      metadata.displayPrimaries[1].y = 0.797f * 50000;
      metadata.displayPrimaries[2].x = 0.131f * 50000;
      metadata.displayPrimaries[2].y = 0.046f * 50000;
      metadata.whitePoint.x = 0.3127f * 50000;
      metadata.whitePoint.y = 0.3290f * 50000;

      metadata.maxDisplayLuminance = 1000;
      metadata.minDisplayLuminance = 0;

      // These are content-specific metadata parameters that this interface doesn't give us
      metadata.maxContentLightLevel = 0;
      metadata.maxFrameAverageLightLevel = 0;
      metadata.maxFullFrameLuminance = 0;

      return true;
    }
  };

  std::shared_ptr<display_t> display(platf::mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    @autoreleasepool {
      if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::videotoolbox) {
        BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
        return nullptr;
      }

      auto display = std::make_shared<sck_display_t>();

      BOOST_LOG(debug) << "Waking display for capture selector ["sv << display_name << ']';
      if (!display_device::wake_display(display_name, 1s)) {
        BOOST_LOG(debug) << "Display wake attempt did not expose the requested display ["sv << display_name << ']';
      }

      display->display_power_guard = display_device::keep_display_awake("Sunshine display capture");
      if (display->display_power_guard) {
        BOOST_LOG(debug) << "Keeping display awake for capture"sv;
      } else {
        BOOST_LOG(debug) << "Unable to create display sleep prevention assertion"sv;
      }

      // Default to main display
      display->display_id = CGMainDisplayID();

      if (const auto configured_display_id {parse_display_id(display_name)}) {
        display->display_id = *configured_display_id;
      } else if (!display_name.empty()) {
        BOOST_LOG(warning) << "Configured display ["sv << display_name
                           << "] is not a valid macOS capture display id. Falling back to main display ["sv
                           << display->display_id << "]."sv;
      }

      // Print all displays available with their names and ids
      BOOST_LOG(debug) << "Detecting displays"sv;
      for (const auto &device : display_device::enumerate_devices()) {
        if (device.m_display_name.empty()) {
          continue;
        }

        BOOST_LOG(debug) << "Detected display: "sv << device.m_friendly_name
                        << " (id: "sv << device.m_display_name << ") connected: true"sv;
      }

      BOOST_LOG(info) << "Configuring selected display ("sv << display->display_id << ") to stream"sv;

      display->sc = sck_video_capture_create(hwdevice_type, display_name, config);
      if (!display->sc) {
        BOOST_LOG(error) << "ScreenCaptureKit init failed."sv;
        return nullptr;
      }

      display->width = display->sc->stream_properties.width;
      display->height = display->sc->stream_properties.height;
      // We also need set env_width and env_height for absolute mouse coordinates
      display->env_width = display->width;
      display->env_height = display->height;

      BOOST_LOG(debug) << "ScreenCaptureKit: resolution is " << display->width << "x" << display->height;

      if (display->init(config)) {
        return nullptr;
      }

      return display;
    }
  }

  std::vector<std::string> display_names(mem_type_e hwdevice_type) {
    std::vector<std::string> display_names;
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::videotoolbox) {
      return display_names;
    }

    const auto devices {display_device::enumerate_devices()};
    display_names.reserve(devices.size());
    for (const auto &device : devices) {
      if (!device.m_display_name.empty()) {
        display_names.emplace_back(device.m_display_name);
      }
    }

    return display_names;
  }

  /**
   * @brief Report whether encoder backends should be probed again before streaming.
   *
   * @return Always `false` on macOS.
   */
  bool needs_encoder_reenumeration() {
    return false;
  }
}  // namespace platf
