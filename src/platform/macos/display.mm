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
#include <thread>

// local includes
#include "src/config.h"
#include "src/display_device.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/av_video.h"
#include "src/platform/macos/misc.h"
#include "src/platform/macos/nv12_zero_device.h"
#import "src/platform/macos/sc_capture.h"
#include "src/utility.h"

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

    OSType videotoolbox_pixel_format(const video::config_t &config) {
      const auto colorspace {video::colorspace_from_client_config(config, false)};
      return colorspace.bit_depth == 10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    }

    /**
     * @brief Derive a steady_clock capture timestamp from a sample buffer's presentation timestamp.
     *
     * The PTS is on the mach host clock, so back-dating steady_clock::now() by the frame's age on
     * that clock includes AVFoundation's internal queueing delay in the reported frame latency.
     * @param sample_buffer Sample buffer received from AVFoundation.
     * @return Capture timestamp for the frame.
     */
    std::chrono::steady_clock::time_point frame_timestamp_from_pts(CMSampleBufferRef sample_buffer) {
      auto timestamp = std::chrono::steady_clock::now();
      const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample_buffer);
      if (CMTIME_IS_NUMERIC(pts)) {
        const CMTime frame_age = CMTimeSubtract(CMClockGetTime(CMClockGetHostTimeClock()), pts);
        const double frame_age_s = CMTimeGetSeconds(frame_age);
        if (frame_age_s > 0) {
          timestamp -= std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(frame_age_s));
        }
      }
      return timestamp;
    }

    // Upper bound on how long the ScreenCaptureKit capture loop waits for a screenshot
    // before checking for interrupt requests again.
    constexpr auto SCKIT_SCREENSHOT_POLL_INTERVAL_NS = NSEC_PER_SEC / 60;

    /**
     * @brief Wrap a captured sample buffer into a Sunshine image.
     *
     * @param sample_buffer Sample buffer received from the capture backend.
     * @param img Image object to populate.
     * @return `true` when the sample buffer contained a usable pixel buffer.
     */
    bool process_frame(CMSampleBufferRef sample_buffer, img_t *img) {
      auto pixel_buffer = CMSampleBufferGetImageBuffer(sample_buffer);
      if (!pixel_buffer) {
        return false;
      }

      auto new_sample_buffer = std::make_shared<av_sample_buf_t>(sample_buffer);
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
      img->row_pitch = CVPixelBufferIsPlanar(new_pixel_buffer->buf) ?
                         (int) CVPixelBufferGetBytesPerRowOfPlane(new_pixel_buffer->buf, 0) :
                         (int) CVPixelBufferGetBytesPerRow(new_pixel_buffer->buf);
      img->pixel_pitch = img->row_pitch / img->width;
      img->frame_timestamp = frame_timestamp_from_pts(sample_buffer);

      old_data_retainer = nullptr;
      return true;
    }

    /**
     * @brief Zero-fill all planes of a pixel buffer.
     *
     * @param pixel_buffer Pixel buffer to clear.
     */
    void clear_pixel_buffer(CVPixelBufferRef pixel_buffer) {
      CVPixelBufferLockBaseAddress(pixel_buffer, 0);

      if (CVPixelBufferIsPlanar(pixel_buffer)) {
        for (size_t plane = 0; plane < CVPixelBufferGetPlaneCount(pixel_buffer); ++plane) {
          auto *base = static_cast<std::uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, plane));
          auto bytes_per_row = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, plane);
          auto height = CVPixelBufferGetHeightOfPlane(pixel_buffer, plane);
          std::memset(base, 0, bytes_per_row * height);
        }
      } else {
        auto *base = static_cast<std::uint8_t *>(CVPixelBufferGetBaseAddress(pixel_buffer));
        std::memset(base, 0, CVPixelBufferGetBytesPerRow(pixel_buffer) * CVPixelBufferGetHeight(pixel_buffer));
      }

      CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
    }

    /**
     * @brief Synthesize a blank frame for capture backends that only deliver frames on screen changes.
     *
     * @param img Image object to populate.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param pixel_format Pixel format of the synthesized frame.
     * @param backend_name Capture backend name used in log messages.
     * @return 0 on success; nonzero on failure.
     */
    int make_dummy_img(img_t *img, int width, int height, OSType pixel_format, std::string_view backend_name) {
      CVPixelBufferRef pixel_buffer = nullptr;
      NSDictionary *attrs = @{
        (NSString *) kCVPixelBufferIOSurfacePropertiesKey: @ {},
      };

      auto status = CVPixelBufferCreate(
        kCFAllocatorDefault,
        width,
        height,
        pixel_format,
        (__bridge CFDictionaryRef) attrs,
        &pixel_buffer
      );

      if (status != kCVReturnSuccess || !pixel_buffer) {
        BOOST_LOG(error) << backend_name << " dummy_img: failed to create pixel buffer"sv;
        return 1;
      }

      clear_pixel_buffer(pixel_buffer);

      CMVideoFormatDescriptionRef format_desc = nullptr;
      status = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixel_buffer, &format_desc);
      if (status != noErr || !format_desc) {
        CVPixelBufferRelease(pixel_buffer);
        BOOST_LOG(error) << backend_name << " dummy_img: failed to create format description"sv;
        return 1;
      }

      CMSampleTimingInfo timing = {kCMTimeInvalid, kCMTimeInvalid, kCMTimeInvalid};
      CMSampleBufferRef sample_buffer = nullptr;
      status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault, pixel_buffer, YES, nullptr, nullptr, format_desc, &timing, &sample_buffer);
      CFRelease(format_desc);

      if (status != noErr || !sample_buffer) {
        CVPixelBufferRelease(pixel_buffer);
        BOOST_LOG(error) << backend_name << " dummy_img: failed to create sample buffer"sv;
        return 1;
      }

      auto ret = process_frame(sample_buffer, img) ? 0 : 1;
      CFRelease(sample_buffer);
      CVPixelBufferRelease(pixel_buffer);

      return ret;
    }
  }  // namespace

  /**
   * @brief macOS display capture source and image buffers.
   */
  struct av_display_t: public display_t {
    AVVideo *av_capture {};  ///< AV capture.
    CGDirectDisplayID display_id {};  ///< Display ID.
    std::unique_ptr<display_device::DisplayPowerGuardInterface> display_power_guard;  ///< Display power guard.

    ~av_display_t() override {
      [av_capture release];
    }

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto signal = [av_capture capture:^(CMSampleBufferRef sampleBuffer) {
        const auto frame_timestamp = frame_timestamp_from_pts(sampleBuffer);
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
        img_out->frame_timestamp = frame_timestamp;

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

        device->init(static_cast<void *>(av_capture), pix_fmt, setResolution, setPixelFormat);

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
    static void setResolution(void *display, int width, int height) {
      [static_cast<AVVideo *>(display) setFrameWidth:width frameHeight:height];
    }

    /**
     * @brief Set pixel format.
     *
     * @param display Display object or identifier associated with the operation.
     * @param pixelFormat Pixel format.
     */
    static void setPixelFormat(void *display, OSType pixelFormat) {
      static_cast<AVVideo *>(display).pixelFormat = pixelFormat;
    }
  };

  /**
   * @brief ScreenCaptureKit-based macOS display capture source.
   */
  struct sc_display_t: public display_t {
    SCCapture *sc_capture {};  ///< ScreenCaptureKit capture controller.
    CGDirectDisplayID display_id {};  ///< Display ID.
    std::unique_ptr<display_device::DisplayPowerGuardInterface> display_power_guard;  ///< Display power guard.

    ~sc_display_t() override {
      [sc_capture stopCapture];
      [sc_capture release];
    }

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto signal = [sc_capture captureVideo];
      if (!signal) {
        BOOST_LOG(error) << "SCCapture failed to start video capture"sv;
        return capture_e::error;
      }
      dispatch_retain(signal);

      auto frame_signal = sc_capture.frameSignal;
      if (!frame_signal) {
        BOOST_LOG(error) << "SCCapture failed to create frame signal"sv;
        dispatch_release(signal);
        [sc_capture stopCapture];
        return capture_e::error;
      }
      dispatch_retain(frame_signal);

      auto release_signals = util::fail_guard([signal, frame_signal]() {
        dispatch_release(frame_signal);
        dispatch_release(signal);
      });

      const auto frame_interval = std::chrono::nanoseconds(NSEC_PER_SEC / std::max(sc_capture.frameRate, 1));
      auto next_frame_slot = std::chrono::steady_clock::now();

      while (true) {
        // Pace the polling to the capture frame rate and keep exactly one request in flight.
        std::this_thread::sleep_until(next_frame_slot);
        next_frame_slot += frame_interval;
        if (next_frame_slot < std::chrono::steady_clock::now()) {
          next_frame_slot = std::chrono::steady_clock::now();
        }
        [sc_capture requestScreenshotSampleBuffer];

        auto frame_status = dispatch_semaphore_wait(frame_signal, dispatch_time(DISPATCH_TIME_NOW, SCKIT_SCREENSHOT_POLL_INTERVAL_NS));
        (void) frame_status;
        if (dispatch_semaphore_wait(signal, DISPATCH_TIME_NOW) == 0) {
          break;
        }

        CMSampleBufferRef sample_buffer = [sc_capture copyLatestSampleBuffer];

        if (!sample_buffer) {
          std::shared_ptr<img_t> probe_img;
          if (!pull_free_image_cb(probe_img)) {
            [sc_capture stopCapture];
            break;
          }
          continue;
        }

        auto release_sample_buffer = util::fail_guard([sample_buffer]() {
          CFRelease(sample_buffer);
        });

        std::shared_ptr<img_t> img_out;
        if (!pull_free_image_cb(img_out)) {
          [sc_capture stopCapture];
          break;
        }

        if (!process_frame(sample_buffer, img_out.get())) {
          continue;
        }

        if (!push_captured_image_cb(std::move(img_out), true)) {
          [sc_capture stopCapture];
          break;
        }
      }

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
        sc_capture.pixelFormat = kCVPixelFormatType_32BGRA;

        return std::make_unique<avcodec_encode_device_t>();
      } else if (pix_fmt == pix_fmt_e::nv12 || pix_fmt == pix_fmt_e::p010) {
        auto device = std::make_unique<nv12_zero_device>();

        device->init(static_cast<void *>(sc_capture), pix_fmt, setResolution, setPixelFormat);

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
        // A non-zero return value indicates failure to the calling function.
        return 1;
      }

      // ScreenCaptureKit only delivers frames on screen updates, so synthesize a blank frame
      // instead of waiting for one.
      return make_dummy_img(img, sc_capture.frameWidth, sc_capture.frameHeight, sc_capture.pixelFormat, "SCCapture"sv);
    }

    /**
     * @brief Set the capture output resolution.
     *
     * @param display Display object or identifier associated with the operation.
     * @param width Frame or display width in pixels.
     * @param height Frame or display height in pixels.
     */
    static void setResolution(void *display, int width, int height) {
      [static_cast<SCCapture *>(display) setFrameWidth:width frameHeight:height];
    }

    /**
     * @brief Set pixel format.
     *
     * @param display Display object or identifier associated with the operation.
     * @param pixelFormat Pixel format.
     */
    static void setPixelFormat(void *display, OSType pixelFormat) {
      static_cast<SCCapture *>(display).pixelFormat = pixelFormat;
    }
  };

  std::shared_ptr<display_t> display(platf::mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::videotoolbox) {
      BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    BOOST_LOG(debug) << "Waking display for capture selector ["sv << display_name << ']';
    if (!display_device::wake_display(display_name, 1s)) {
      BOOST_LOG(debug) << "Display wake attempt did not expose the requested display ["sv << display_name << ']';
    }

    auto display_power_guard = display_device::keep_display_awake("Sunshine display capture");
    if (display_power_guard) {
      BOOST_LOG(debug) << "Keeping display awake for capture"sv;
    } else {
      BOOST_LOG(debug) << "Unable to create display sleep prevention assertion"sv;
    }

    // Default to main display
    CGDirectDisplayID display_id = CGMainDisplayID();

    if (const auto configured_display_id {parse_display_id(display_name)}) {
      display_id = *configured_display_id;
    } else if (!display_name.empty()) {
      BOOST_LOG(warning) << "Configured display ["sv << display_name
                         << "] is not a valid macOS capture display id. Falling back to main display ["sv
                         << display_id << "]."sv;
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

    BOOST_LOG(info) << "Configuring selected display ("sv << display_id << ") to stream"sv;

    // Prefer ScreenCaptureKit: it composites the cursor into frames server-side, which avoids
    // an AVCaptureScreenInput bug where a hidden cursor never reappears in the captured frames.
    if (@available(macOS 14.0, *)) {
      if ([SCCapture isAvailable]) {
        auto display = std::make_shared<sc_display_t>();
        display->display_id = display_id;
        display->display_power_guard = std::move(display_power_guard);
        display->sc_capture = [[SCCapture alloc] initWithDisplay:display_id frameRate:config.framerate];

        if (display->sc_capture) {
          display->width = display->sc_capture.frameWidth;
          display->height = display->sc_capture.frameHeight;
          // We also need set env_width and env_height for absolute mouse coordinates
          display->env_width = display->width;
          display->env_height = display->height;

          if (hwdevice_type == platf::mem_type_e::videotoolbox) {
            const auto pixel_format {videotoolbox_pixel_format(config)};
            [display->sc_capture setFrameWidth:config.width frameHeight:config.height];
            display->sc_capture.pixelFormat = pixel_format;
          }

          return display;
        }

        display_power_guard = std::move(display->display_power_guard);
        BOOST_LOG(error) << "SCCapture setup failed, trying AVFoundation..."sv;
      }
    }

    auto display = std::make_shared<av_display_t>();
    display->display_id = display_id;
    display->display_power_guard = std::move(display_power_guard);

    display->av_capture = [[AVVideo alloc] initWithDisplay:display->display_id frameRate:config.framerate];

    if (!display->av_capture) {
      BOOST_LOG(error) << "Video setup failed."sv;
      return nullptr;
    }

    display->width = display->av_capture.frameWidth;
    display->height = display->av_capture.frameHeight;
    // We also need set env_width and env_height for absolute mouse coordinates
    display->env_width = display->width;
    display->env_height = display->height;

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
