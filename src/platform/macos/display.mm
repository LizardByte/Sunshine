/**
 * @file src/platform/macos/display.mm
 * @brief Definitions for display capture on macOS.
 */

// standard includes
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

// platform includes
#import <AppKit/AppKit.h>

// local includes
#include "src/config.h"
#include "src/display_device.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/av_video.h"
#include "src/platform/macos/misc.h"
#include "src/platform/macos/nv12_zero_device.h"
#include "src/platform/macos/sc_video.h"

// Avoid conflict between AVFoundation and libavutil both defining AVMediaType
/**
 * @def AVMediaType
 * @brief Macro for AV media type.
 */
#define AVMediaType AVMediaType_FFmpeg
#include "src/video.h"
#undef AVMediaType

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

    /**
     * @brief Return the union of active CoreGraphics display bounds.
     *
     * @param fallback_display Display whose bounds are used if enumeration fails.
     * @return Full active desktop bounds in CoreGraphics coordinates.
     */
    CGRect active_desktop_bounds(CGDirectDisplayID fallback_display) {
      std::uint32_t display_count {};
      if (CGGetActiveDisplayList(0, nullptr, &display_count) != kCGErrorSuccess || display_count == 0) {
        return CGDisplayBounds(fallback_display);
      }

      std::vector<CGDirectDisplayID> displays(display_count);
      if (CGGetActiveDisplayList(display_count, displays.data(), &display_count) != kCGErrorSuccess || display_count == 0) {
        return CGDisplayBounds(fallback_display);
      }

      auto bounds = CGDisplayBounds(displays.front());
      for (std::uint32_t index = 1; index < display_count; ++index) {
        bounds = CGRectUnion(bounds, CGDisplayBounds(displays[index]));
      }
      return bounds;
    }

    OSType videotoolbox_pixel_format(const video::config_t &config) {
      const auto colorspace {video::colorspace_from_client_config(config, false)};
      return colorspace.bit_depth == 10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    }

    /**
     * @brief Return the EDR headroom macOS reports for a display.
     *
     * EDR 1.0 is reference white, which ScreenCaptureKit's canonical HDR mode maps to 100 nits.
     *
     * @param display_id Display to query.
     * @return Maximum potential EDR component value, or 1.0 (SDR only) if the display is not found.
     */
    double edr_headroom(CGDirectDisplayID display_id) {
      @autoreleasepool {
        for (NSScreen *screen in NSScreen.screens) {
          if ([screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue] == display_id) {
            return screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
          }
        }
      }
      return 1.0;
    }

  }  // namespace

  std::chrono::steady_clock::time_point capture_time_point(double age_seconds, std::chrono::steady_clock::time_point now) {
    if (age_seconds < 0.0 || age_seconds > 0.05) {
      return now;
    }
    return now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(age_seconds));
  }

  bool edr_hdr_metadata(double edr_headroom, double sdr_white_nits, SS_HDR_METADATA &metadata) {
    std::memset(&metadata, 0, sizeof(metadata));
    if (edr_headroom <= 1.0) {
      return false;
    }

    // Chromaticities are in 0.00002 units and the minimum luminance in 0.0001 nits.
    metadata.displayPrimaries[0] = {32000, 16500};  // red   0.640, 0.330
    metadata.displayPrimaries[1] = {15000, 30000};  // green 0.300, 0.600
    metadata.displayPrimaries[2] = {7500, 3000};  // blue  0.150, 0.060
    metadata.whitePoint = {15635, 16450};  // D65   0.3127, 0.3290
    const auto peak_nits = static_cast<uint16_t>(std::min(edr_headroom * sdr_white_nits, 10000.0));
    metadata.maxDisplayLuminance = peak_nits;
    metadata.minDisplayLuminance = 50;  // 0.005 nits
    metadata.maxContentLightLevel = peak_nits;
    metadata.maxFrameAverageLightLevel = peak_nits;
    metadata.maxFullFrameLuminance = peak_nits;
    return true;
  }

  /**
   * @brief macOS display capture source and image buffers.
   */
  struct av_display_t: public display_t {
    AVVideo *av_capture {};  ///< AV capture.
    CGDirectDisplayID display_id {};  ///< Display ID.
    std::unique_ptr<display_device::DisplayPowerGuardInterface> display_power_guard;  ///< Display power guard.
    double hdr_headroom {1.0};  ///< EDR headroom when HDR capture is available, otherwise 1.0.
    double hdr_sdr_white {203.0};  ///< Luminance in nits that SDR white is streamed at in HDR.

    ~av_display_t() override {
      [av_capture release];
    }

    /**
     * @brief Report whether the display can be captured in HDR.
     *
     * @return `true` when ScreenCaptureKit HDR capture is available and the display has EDR headroom.
     */
    bool is_hdr() override {
      return hdr_headroom > 1.0;
    }

    /**
     * @brief Describe the display's HDR capabilities for the client.
     *
     * @param metadata Receives the HDR metadata.
     * @return `true` when the display is HDR and `metadata` was filled.
     */
    bool get_hdr_metadata(SS_HDR_METADATA &metadata) override {
      return edr_hdr_metadata(hdr_headroom, hdr_sdr_white, metadata);
    }

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto signal = [av_capture capture:^(CMSampleBufferRef sampleBuffer) {
        auto new_sample_buffer = std::make_shared<av_sample_buf_t>(sampleBuffer);
        auto new_pixel_buffer = std::make_shared<av_pixel_buf_t>(new_sample_buffer->buf);

        std::shared_ptr<img_t> img_out;
        if (!pull_free_image_cb(img_out)) {
          // got interrupt signal
          // returning false here stops capture backend
          return false;
        }
        auto av_img = std::static_pointer_cast<av_img_t>(img_out);

        auto old_data_retainer = std::make_shared<temp_retain_av_img_t>(
          av_img->sample_buffer,
          av_img->pixel_buffer,
          img_out->data
        );

        av_img->sample_buffer = new_sample_buffer;
        av_img->pixel_buffer = new_pixel_buffer;
        img_out->data = new_pixel_buffer->data();

        img_out->width = (int) CVPixelBufferGetWidth(new_pixel_buffer->buf);
        img_out->height = (int) CVPixelBufferGetHeight(new_pixel_buffer->buf);
        img_out->row_pitch = (int) CVPixelBufferGetBytesPerRow(new_pixel_buffer->buf);
        img_out->pixel_pitch = img_out->row_pitch / img_out->width;
        const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        const double age = CMTIME_IS_NUMERIC(pts) ? CMTimeGetSeconds(CMTimeSubtract(CMClockGetTime(CMClockGetHostTimeClock()), pts)) : -1.0;
        img_out->frame_timestamp = capture_time_point(age, std::chrono::steady_clock::now());

        old_data_retainer = nullptr;

        if (!push_captured_image_cb(std::move(img_out), true)) {
          // got interrupt signal
          // returning false here stops capture backend
          return false;
        }

        return true;
      }];

      // FIXME: We should time out if an image isn't returned for a while
      dispatch_semaphore_wait(signal, DISPATCH_TIME_FOREVER);

      return capture_e::ok;
    }

    /**
     * @brief Allocate an image buffer compatible with this display backend.
     *
     * @return Allocated img object, or null when unavailable.
     */
    std::shared_ptr<img_t> alloc_img() override {
      return std::make_shared<av_img_t>();
    }

    /**
     * @brief Create AVCodec encode device.
     *
     * @param pix_fmt Sunshine pixel format to convert or allocate for.
     * @return Constructed AVCodec encode device object.
     */
    std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e pix_fmt) override {
      if (pix_fmt == pix_fmt_e::yuv420p) {
        av_capture.pixelFormat = kCVPixelFormatType_32BGRA;

        return std::make_unique<avcodec_encode_device_t>();
      } else if (pix_fmt == pix_fmt_e::nv12 || pix_fmt == pix_fmt_e::p010) {
        auto device = std::make_unique<nv12_zero_device>();

        device->init(av_capture, pix_fmt, setResolution, setPixelFormat);

        return device;
      } else {
        BOOST_LOG(error) << "Unsupported Pixel Format."sv;
        return nullptr;
      }
    }

    /**
     * @brief Populate a fallback image when real capture data is unavailable.
     *
     * @param img Image or frame object to read from or populate.
     * @return Capture status reported to the streaming pipeline.
     */
    int dummy_img(img_t *img) override {
      if (!platf::is_screen_capture_allowed()) {
        // If we don't have the screen capture permission, this function will hang
        // indefinitely without doing anything useful. Exit instead to avoid this.
        // A non-zero return value indicates failure to the calling function.
        return 1;
      }

      auto signal = [av_capture capture:^(CMSampleBufferRef sampleBuffer) {
        auto new_sample_buffer = std::make_shared<av_sample_buf_t>(sampleBuffer);
        auto new_pixel_buffer = std::make_shared<av_pixel_buf_t>(new_sample_buffer->buf);

        auto av_img = (av_img_t *) img;

        auto old_data_retainer = std::make_shared<temp_retain_av_img_t>(
          av_img->sample_buffer,
          av_img->pixel_buffer,
          img->data
        );

        av_img->sample_buffer = new_sample_buffer;
        av_img->pixel_buffer = new_pixel_buffer;
        img->data = new_pixel_buffer->data();

        img->width = (int) CVPixelBufferGetWidth(new_pixel_buffer->buf);
        img->height = (int) CVPixelBufferGetHeight(new_pixel_buffer->buf);
        img->row_pitch = (int) CVPixelBufferGetBytesPerRow(new_pixel_buffer->buf);
        img->pixel_pitch = img->row_pitch / img->width;

        old_data_retainer = nullptr;

        // returning false here stops capture backend
        return false;
      }];

      dispatch_semaphore_wait(signal, DISPATCH_TIME_FOREVER);

      return 0;
    }

    /**
     * A bridge from the pure C++ code of the hwdevice_t class to the pure Objective C code.
     *
     * display --> an opaque pointer to an object of this class
     * width --> the intended capture width
     * height --> the intended capture height
     * @param display Display object or identifier associated with the operation.
     * @param width Frame or display width in pixels.
     * @param height Frame or display height in pixels.
     */
    static void setResolution(AVVideo *display, int width, int height) {
      [display setFrameWidth:width frameHeight:height];
    }

    /**
     * @brief Set pixel format.
     *
     * @param display Display object or identifier associated with the operation.
     * @param pixelFormat Pixel format.
     */
    static void setPixelFormat(AVVideo *display, OSType pixelFormat) {
      display.pixelFormat = pixelFormat;
    }
  };

  std::shared_ptr<display_t> display(platf::mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::videotoolbox) {
      BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    auto display = std::make_shared<av_display_t>();

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

    const auto selected_display_bounds = CGDisplayBounds(display->display_id);
    const auto desktop_bounds = active_desktop_bounds(display->display_id);
    display->offset_x = static_cast<int>(selected_display_bounds.origin.x);
    display->offset_y = static_cast<int>(selected_display_bounds.origin.y);
    display->logical_width = static_cast<int>(selected_display_bounds.size.width);
    display->logical_height = static_cast<int>(selected_display_bounds.size.height);
    display->env_offset_x = static_cast<int>(desktop_bounds.origin.x);
    display->env_offset_y = static_cast<int>(desktop_bounds.origin.y);
    display->env_logical_width = static_cast<int>(desktop_bounds.size.width);
    display->env_logical_height = static_cast<int>(desktop_bounds.size.height);
    display->env_width = display->env_logical_width;
    display->env_height = display->env_logical_height;

    BOOST_LOG(debug) << "Selected display bounds: "sv << display->offset_x << 'x' << display->offset_y << ' '
                     << display->logical_width << 'x' << display->logical_height;
    BOOST_LOG(debug) << "Active desktop bounds: "sv << display->env_offset_x << 'x' << display->env_offset_y << ' '
                     << display->env_logical_width << 'x' << display->env_logical_height;

    // ScreenCaptureKit (macOS 15+) supports HDR capture and higher frame rates than AVCaptureScreenInput.
    if (@available(macOS 15.0, *)) {
      auto *sc_capture = [[SCVideo alloc] initWithDisplay:display->display_id frameRate:config.framerate];
      if (sc_capture) {
        const auto headroom = edr_headroom(display->display_id);
        if (headroom > 1.0) {
          display->hdr_headroom = headroom;
          sc_capture.hdr = config.dynamicRange > 0;
          display->hdr_sdr_white = config::video.vt.vt_hdr_sdr_white;
          sc_capture.sdrWhiteNits = static_cast<float>(display->hdr_sdr_white);
        }
        BOOST_LOG(info) << "Using ScreenCaptureKit capture (EDR headroom "sv << headroom << ", HDR "sv
                        << (sc_capture.hdr ? "on"sv : "off"sv) << ')';
      }
      display->av_capture = sc_capture;
    }
    if (!display->av_capture) {
      display->av_capture = [[AVVideo alloc] initWithDisplay:display->display_id frameRate:config.framerate];
    }

    if (!display->av_capture) {
      BOOST_LOG(error) << "Video setup failed."sv;
      return nullptr;
    }

    display->width = display->av_capture.frameWidth;
    display->height = display->av_capture.frameHeight;

    if (hwdevice_type == platf::mem_type_e::videotoolbox) {
      const auto pixel_format {videotoolbox_pixel_format(config)};
      [display->av_capture setFrameWidth:config.width frameHeight:config.height];
      display->av_capture.pixelFormat = pixel_format;
    }

    return display;
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
   * @return Always `true` because macOS GPU changes are not tracked by this backend.
   */
  bool needs_encoder_reenumeration() {
    // We don't track GPU state, so we will always reenumerate. Fortunately, it is fast on macOS.
    return true;
  }
}  // namespace platf
