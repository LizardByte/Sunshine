/**
 * @file src/platform/macos/display.mm
 * @brief Definitions for display capture on macOS.
 */

// standard includes
#include <cstdlib>
#include <sstream>
#include <thread>

// platform includes
#include <IOKit/pwr_mgt/IOPMLib.h>

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/av_video.h"
#include "src/platform/macos/misc.h"
#include "src/platform/macos/nv12_zero_device.h"
#include "src/platform/macos/sc_video.h"

// Avoid conflict between AVFoundation and libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "src/video.h"
#undef AVMediaType

namespace fs = std::filesystem;

namespace platf {
  using namespace std::literals;

  namespace {
    const char *cg_error_name(CGError error) {
      switch (error) {
        case kCGErrorSuccess:
          return "success";
        case kCGErrorFailure:
          return "failure";
        case kCGErrorIllegalArgument:
          return "illegal argument";
        case kCGErrorInvalidConnection:
          return "invalid connection";
        case kCGErrorInvalidContext:
          return "invalid context";
        case kCGErrorCannotComplete:
          return "cannot complete";
        case kCGErrorNotImplemented:
          return "not implemented";
        case kCGErrorRangeCheck:
          return "range check";
        case kCGErrorTypeCheck:
          return "type check";
        case kCGErrorInvalidOperation:
          return "invalid operation";
        case kCGErrorNoneAvailable:
          return "none available";
        default:
          return "unknown";
      }
    }

    std::string format_rect(CGRect rect) {
      std::ostringstream formatted;
      formatted << '(' << rect.origin.x << ',' << rect.origin.y << ") "
                << rect.size.width << 'x' << rect.size.height;
      return formatted.str();
    }

    std::string display_mode_summary(CGDisplayModeRef mode) {
      if (!mode) {
        return "<none>";
      }

      std::ostringstream formatted;
      formatted << CGDisplayModeGetWidth(mode) << 'x' << CGDisplayModeGetHeight(mode)
                << " points, " << CGDisplayModeGetPixelWidth(mode) << 'x'
                << CGDisplayModeGetPixelHeight(mode) << " pixels";

      const auto refresh_rate = CGDisplayModeGetRefreshRate(mode);
      if (refresh_rate > 0) {
        formatted << ", " << refresh_rate << " Hz";
      }

      return formatted.str();
    }

    void log_display_diagnostic(CGDirectDisplayID display_id, const char *source) {
      NSString *display_name = [AVVideo getDisplayName:display_id];
      const char *display_name_utf8 = display_name ? display_name.UTF8String : "<unknown>";
      CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display_id);

      BOOST_LOG(info) << "Display diagnostic ["sv << source << "]: id: "sv << display_id
                      << ", name: "sv << display_name_utf8
                      << ", main: "sv << (display_id == CGMainDisplayID())
                      << ", active: "sv << CGDisplayIsActive(display_id)
                      << ", online: "sv << CGDisplayIsOnline(display_id)
                      << ", asleep: "sv << CGDisplayIsAsleep(display_id)
                      << ", built-in: "sv << CGDisplayIsBuiltin(display_id)
                      << ", bounds: "sv << format_rect(CGDisplayBounds(display_id))
                      << ", framebuffer pixels: "sv << CGDisplayPixelsWide(display_id) << 'x' << CGDisplayPixelsHigh(display_id)
                      << ", mode: "sv << display_mode_summary(mode);

      if (mode) {
        CFRelease(mode);
      }
    }

    void log_nsscreen_diagnostics() {
      NSArray<NSScreen *> *screens = [NSScreen screens];

      BOOST_LOG(info) << "NSScreen diagnostics: count: "sv << [screens count];

      for (NSScreen *screen in screens) {
        NSNumber *display_id = screen.deviceDescription[@"NSScreenNumber"];
        NSString *screen_name = screen.localizedName;

        BOOST_LOG(info) << "NSScreen diagnostic: id: "sv << (display_id ? [display_id unsignedIntValue] : 0)
                        << ", name: "sv << (screen_name ? screen_name.UTF8String : "<unknown>")
                        << ", frame: "sv << format_rect(screen.frame)
                        << ", backing scale: "sv << screen.backingScaleFactor;
      }
    }

    void log_display_list_diagnostics(const char *list_name, CGError error, const CGDirectDisplayID *displays, uint32_t count) {
      BOOST_LOG(info) << list_name << ": status: "sv << cg_error_name(error)
                      << " ("sv << error << "), count: "sv << count;

      if (error != kCGErrorSuccess) {
        return;
      }

      for (uint32_t i = 0; i < count; ++i) {
        log_display_diagnostic(displays[i], list_name);
      }
    }

    void log_display_environment_diagnostics() {
      CGDirectDisplayID active_displays[kMaxDisplays];
      CGDirectDisplayID online_displays[kMaxDisplays];
      uint32_t active_display_count = 0;
      uint32_t online_display_count = 0;

      const auto active_error = CGGetActiveDisplayList(kMaxDisplays, active_displays, &active_display_count);
      const auto online_error = CGGetOnlineDisplayList(kMaxDisplays, online_displays, &online_display_count);

      BOOST_LOG(info) << "Main display diagnostic: id: "sv << CGMainDisplayID();
      log_display_list_diagnostics("CGGetActiveDisplayList", active_error, active_displays, active_display_count);
      log_display_list_diagnostics("CGGetOnlineDisplayList", online_error, online_displays, online_display_count);
      log_nsscreen_diagnostics();
    }

    bool has_required_active_display(const std::string &display_name) {
      CGDirectDisplayID displays[kMaxDisplays];
      uint32_t display_count = 0;

      if (CGGetActiveDisplayList(kMaxDisplays, displays, &display_count) != kCGErrorSuccess) {
        return false;
      }

      if (display_name.empty()) {
        return display_count > 0;
      }

      char *end = nullptr;
      const auto selected_display_id = std::strtoul(display_name.c_str(), &end, 10);
      if (!end || *end != '\0') {
        return display_count > 0;
      }

      for (uint32_t i = 0; i < display_count; ++i) {
        if (displays[i] == selected_display_id) {
          return true;
        }
      }

      return false;
    }

    void wake_displays_for_detection(const std::string &display_name) {
      IOPMAssertionID wake_assertion = kIOPMNullAssertionID;
      const auto result = IOPMAssertionDeclareUserActivity(
        CFSTR("Sunshine display detection"),
        kIOPMUserActiveRemote,
        &wake_assertion
      );

      if (result != kIOReturnSuccess) {
        BOOST_LOG(warning) << "Unable to declare remote user activity to wake displays, IOReturn: "sv << result;
        return;
      }

      BOOST_LOG(info) << "Declared remote user activity to wake displays, assertion id: "sv << wake_assertion;

      for (int attempt = 0; attempt < 10 && !has_required_active_display(display_name); ++attempt) {
        std::this_thread::sleep_for(100ms);
      }

      if (!has_required_active_display(display_name)) {
        BOOST_LOG(warning) << "Display wake attempt did not expose the requested display ["sv
                           << display_name << "] in the active display list."sv;
      }

      if (wake_assertion != kIOPMNullAssertionID) {
        const auto release_result = IOPMAssertionRelease(wake_assertion);
        if (release_result != kIOReturnSuccess) {
          BOOST_LOG(warning) << "Unable to release display wake assertion, IOReturn: "sv << release_result;
        }
      }
    }
  }  // namespace

  struct av_display_t: public display_t {
    id<SunshineVideoCapture> av_capture {};
    CGDirectDisplayID display_id {};
    IOPMAssertionID display_sleep_assertion {kIOPMNullAssertionID};

    ~av_display_t() override {
      [av_capture release];

      if (display_sleep_assertion != kIOPMNullAssertionID) {
        const auto result = IOPMAssertionRelease(display_sleep_assertion);
        if (result != kIOReturnSuccess) {
          BOOST_LOG(warning) << "Unable to release display sleep assertion, IOReturn: "sv << result;
        }
      }
    }

    void prevent_display_sleep() {
      if (display_sleep_assertion != kIOPMNullAssertionID) {
        return;
      }

      const auto result = IOPMAssertionCreateWithName(
        kIOPMAssertPreventUserIdleDisplaySleep,
        kIOPMAssertionLevelOn,
        CFSTR("Sunshine display capture"),
        &display_sleep_assertion
      );

      if (result == kIOReturnSuccess) {
        BOOST_LOG(info) << "Created display sleep prevention assertion, assertion id: "sv << display_sleep_assertion;
        return;
      }

      display_sleep_assertion = kIOPMNullAssertionID;
      BOOST_LOG(warning) << "Unable to create display sleep prevention assertion, IOReturn: "sv << result;
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

    std::shared_ptr<img_t> alloc_img() override {
      return std::make_shared<av_img_t>();
    }

    std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e pix_fmt) override {
      if (pix_fmt == pix_fmt_e::yuv420p) {
        av_capture.pixelFormat = kCVPixelFormatType_32BGRA;

        return std::make_unique<avcodec_encode_device_t>();
      } else if (pix_fmt == pix_fmt_e::nv12 || pix_fmt == pix_fmt_e::p010) {
        auto device = std::make_unique<nv12_zero_device>();

        device->init((__bridge void *) av_capture, pix_fmt, setResolution, setPixelFormat);

        return device;
      } else {
        BOOST_LOG(error) << "Unsupported Pixel Format."sv;
        return nullptr;
      }
    }

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
     */
    static void setResolution(void *display, int width, int height) {
      [(__bridge id<SunshineVideoCapture>) display setFrameWidth:width frameHeight:height];
    }

    static void setPixelFormat(void *display, OSType pixelFormat) {
      ((__bridge id<SunshineVideoCapture>) display).pixelFormat = pixelFormat;
    }
  };

  std::shared_ptr<display_t> display(platf::mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::videotoolbox) {
      BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    auto display = std::make_shared<av_display_t>();
    display->prevent_display_sleep();
    wake_displays_for_detection(display_name);

    // Default to main display
    display->display_id = CGMainDisplayID();

    // Print all displays available with it's name and id
    BOOST_LOG(info) << "Detecting displays"sv;
    log_display_environment_diagnostics();

    auto display_array = [AVVideo displayNames];
    bool matched_configured_display = display_name.empty();
    for (NSDictionary *item in display_array) {
      NSNumber *display_id = item[@"id"];
      // We need show display's product name and corresponding display number given by user
      NSString *name = item[@"displayName"];
      // We are using CGGetActiveDisplayList that only returns active displays so hardcoded connected value in log to true
      BOOST_LOG(info) << "Detected display: "sv << name.UTF8String << " (id: "sv << [NSString stringWithFormat:@"%@", display_id].UTF8String << ") connected: true"sv;
      if (!display_name.empty() && std::atoi(display_name.c_str()) == [display_id unsignedIntValue]) {
        display->display_id = [display_id unsignedIntValue];
        matched_configured_display = true;
      }
    }

    if (!matched_configured_display) {
      BOOST_LOG(warning) << "Configured display ["sv << display_name
                         << "] was not found in the active display list. Falling back to main display ["sv
                         << display->display_id << "]."sv;
    }

    log_display_diagnostic(display->display_id, "selected for AVFoundation capture");
    BOOST_LOG(info) << "Configuring selected display ("sv << display->display_id << ") to stream"sv;

    // Prefer ScreenCaptureKit on macOS 12.3+ (AVCaptureScreenInput was
    // deprecated in macOS 13 and is hardcoded to 8-bit BGRA). Fall back to
    // the legacy AVCaptureScreenInput path on older macOS.
    if (@available(macOS 12.3, *)) {
      BOOST_LOG(info) << "Using ScreenCaptureKit capture backend"sv;
      display->av_capture = [[SCVideo alloc] initWithDisplay:display->display_id frameRate:config.framerate];
    } else {
      BOOST_LOG(info) << "Using legacy AVCaptureScreenInput capture backend"sv;
      display->av_capture = [[AVVideo alloc] initWithDisplay:display->display_id frameRate:config.framerate];
    }

    if (!display->av_capture) {
      BOOST_LOG(error) << "Video setup failed."sv;
      return nullptr;
    }

    display->width = display->av_capture.frameWidth;
    display->height = display->av_capture.frameHeight;
    // We also need set env_width and env_height for absolute mouse coordinates
    display->env_width = display->width;
    display->env_height = display->height;

    return display;
  }

  std::vector<std::string> display_names(mem_type_e hwdevice_type) {
    __block std::vector<std::string> display_names;

    auto display_array = [AVVideo displayNames];

    display_names.reserve([display_array count]);
    [display_array enumerateObjectsUsingBlock:^(NSDictionary *_Nonnull obj, NSUInteger idx, BOOL *_Nonnull stop) {
      NSString *name = obj[@"name"];
      display_names.emplace_back(name.UTF8String);
    }];

    return display_names;
  }

  /**
   * @brief Returns if GPUs/drivers have changed since the last call to this function.
   * @return `true` if a change has occurred or if it is unknown whether a change occurred.
   */
  bool needs_encoder_reenumeration() {
    // We don't track GPU state, so we will always reenumerate. Fortunately, it is fast on macOS.
    return true;
  }
}  // namespace platf
