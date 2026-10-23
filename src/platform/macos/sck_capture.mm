/**
 * @file src/platform/macos/sck_capture.mm
 * @brief ScreenCaptureKit window and application capture for macOS.
 */

// standard includes
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// platform includes
#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

// local includes
#include "src/capture_source.h"
#include "src/display_device.h"
#include "src/logging.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/misc.h"
#include "src/platform/macos/nv12_zero_device.h"
#include "src/platform/macos/sck_capture.h"

/**
 * @brief Receives ScreenCaptureKit video and audio samples.
 */
@interface SCKOutput: NSObject <SCStreamOutput, SCStreamDelegate> {
@public
  std::atomic<int> *reason;
  dispatch_semaphore_t signal;
  std::function<bool(CMSampleBufferRef)> *on_video;
}
@property (nonatomic, assign) SCStream *stream;
@end

namespace platf {
  using namespace std::literals;

  /**
   * @brief Why a ScreenCaptureKit stream stopped.
   */
  enum class stop_reason_e : int {
    none,  ///< The stream is still running.
    reinit,  ///< The captured window size changed.
    interrupted,  ///< The captured window or application is gone.
    error,  ///< Capture failed.
  };

  namespace {

    /**
     * @brief Round a pixel count up to a positive even value.
     *
     * @param value Pixel count.
     * @return Even pixel count of at least 2.
     */
    int even_pixels(int value) {
      if (value < 2) {
        return 2;
      }
      return value + (value & 1);
    }

    /**
     * @brief Create a black sample the encoder can use before the first captured frame.
     *
     * @param pixel_width Even pixel width.
     * @param pixel_height Even pixel height.
     * @param format CoreVideo pixel format.
     * @return A retained sample buffer, or nil when allocation fails. The caller releases it.
     */
    CMSampleBufferRef blank_sample(int pixel_width, int pixel_height, OSType format) {
      CVPixelBufferRef pixel = nullptr;
      if (CVPixelBufferCreate(kCFAllocatorDefault, pixel_width, pixel_height, format, nullptr, &pixel) != kCVReturnSuccess || pixel == nullptr) {
        return nullptr;
      }

      CVPixelBufferLockBaseAddress(pixel, 0);
      if (CVPixelBufferIsPlanar(pixel)) {
        const size_t planes = CVPixelBufferGetPlaneCount(pixel);
        for (size_t plane = 0; plane < planes; ++plane) {
          auto *base = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel, plane));
          const size_t bytes = CVPixelBufferGetBytesPerRowOfPlane(pixel, plane) * CVPixelBufferGetHeightOfPlane(pixel, plane);
          if (base != nullptr && bytes > 0) {
            std::memset(base, plane == 0 ? 0 : 128, bytes);
          }
        }
      } else {
        auto *base = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(pixel));
        const size_t bytes = CVPixelBufferGetBytesPerRow(pixel) * CVPixelBufferGetHeight(pixel);
        if (base != nullptr && bytes > 0) {
          std::memset(base, 0, bytes);
        }
      }
      CVPixelBufferUnlockBaseAddress(pixel, 0);

      CMVideoFormatDescriptionRef description = nullptr;
      if (CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixel, &description) != noErr || description == nullptr) {
        CFRelease(pixel);
        return nullptr;
      }

      CMSampleTimingInfo timing {
        kCMTimeInvalid,
        kCMTimeInvalid,
        kCMTimeInvalid,
      };
      CMSampleBufferRef sample = nullptr;
      const OSStatus status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault, pixel, true, nullptr, nullptr, description, &timing, &sample);
      CFRelease(description);
      CFRelease(pixel);
      if (status != noErr) {
        return nullptr;
      }
      return sample;
    }

    /**
     * @brief Fit a window into the client resolution without enlarging it.
     *
     * @param source_width Window width in pixels.
     * @param source_height Window height in pixels.
     * @param max_width Client width. Zero means no limit.
     * @param max_height Client height. Zero means no limit.
     * @param width Output width.
     * @param height Output height.
     */
    void fit_output(int source_width, int source_height, int max_width, int max_height, int &width, int &height) {
      width = even_pixels(source_width);
      height = even_pixels(source_height);
      if (max_width < 2 || max_height < 2 || (width <= max_width && height <= max_height)) {
        return;
      }

      const auto scale = std::min(static_cast<double>(max_width) / width, static_cast<double>(max_height) / height);
      width = std::min(max_width, even_pixels(static_cast<int>(std::lround(width * scale))));
      height = std::min(max_height, even_pixels(static_cast<int>(std::lround(height * scale))));
    }

    /**
     * @brief Copy one captured sample into a pooled image.
     *
     * @param sample_buffer Captured video sample.
     * @param pull_free_image_cb Callback that supplies a free image.
     * @param push_captured_image_cb Callback that publishes the filled image.
     * @return `false` when capture should stop.
     */
    bool publish_sample(
      CMSampleBufferRef sample_buffer,
      const display_t::pull_free_image_cb_t &pull_free_image_cb,
      const display_t::push_captured_image_cb_t &push_captured_image_cb
    ) {
      auto new_sample_buffer = std::make_shared<av_sample_buf_t>(sample_buffer);
      auto new_pixel_buffer = std::make_shared<av_pixel_buf_t>(new_sample_buffer->buf);

      std::shared_ptr<img_t> img_out;
      if (!pull_free_image_cb(img_out)) {
        return false;
      }

      auto av_img = std::static_pointer_cast<av_img_t>(img_out);
      auto old_data_retainer = std::make_shared<temp_retain_av_img_t>(av_img->sample_buffer, av_img->pixel_buffer, img_out->data);
      av_img->sample_buffer = new_sample_buffer;
      av_img->pixel_buffer = new_pixel_buffer;
      img_out->data = new_pixel_buffer->data();
      img_out->width = static_cast<int>(CVPixelBufferGetWidth(new_pixel_buffer->buf));
      img_out->height = static_cast<int>(CVPixelBufferGetHeight(new_pixel_buffer->buf));
      img_out->row_pitch = static_cast<int>(CVPixelBufferGetBytesPerRow(new_pixel_buffer->buf));
      img_out->pixel_pitch = img_out->row_pitch / img_out->width;
      old_data_retainer.reset();
      return push_captured_image_cb(std::move(img_out), true);
    }

    /**
     * @brief Fetch the windows, displays, and applications ScreenCaptureKit can capture.
     *
     * @return Shareable content, or nil when enumeration fails. The caller owns a retain.
     */
    SCShareableContent *copy_shareable_content() {
      __block SCShareableContent *content = nil;
      auto *signal = dispatch_semaphore_create(0);
      [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                                 onScreenWindowsOnly:NO
                                                   completionHandler:^(SCShareableContent *shareable, NSError *error) {
                                                     if (error != nil) {
                                                       BOOST_LOG(warning) << "ScreenCaptureKit enumeration failed: "sv << error.localizedDescription.UTF8String;
                                                     } else {
                                                       content = [shareable retain];
                                                     }
                                                     dispatch_semaphore_signal(signal);
                                                   }];

      if (dispatch_semaphore_wait(signal, dispatch_time(DISPATCH_TIME_NOW, 3LL * NSEC_PER_SEC)) != 0) {
        BOOST_LOG(warning) << "Timed out while listing shareable windows"sv;
      }
      dispatch_release(signal);
      return content;
    }

    /**
     * @brief Convert a ScreenCaptureKit window to the shared snapshot used for matching.
     *
     * @param window Window returned by ScreenCaptureKit.
     * @return Snapshot. A window without a positive size is left at zero size so matching ignores it.
     */
    capture_source::window_snapshot_t snapshot_window(SCWindow *window) {
      capture_source::window_snapshot_t snapshot;
      snapshot.window_id = window.windowID;
      if (window.title != nil) {
        snapshot.title = window.title.UTF8String;
      }
      if (window.owningApplication != nil) {
        snapshot.pid = window.owningApplication.processID;
        if (window.owningApplication.bundleIdentifier != nil) {
          snapshot.bundle_id = window.owningApplication.bundleIdentifier.UTF8String;
        }
      }
      snapshot.x = static_cast<int>(std::lround(window.frame.origin.x));
      snapshot.y = static_cast<int>(std::lround(window.frame.origin.y));
      snapshot.width = static_cast<int>(std::lround(window.frame.size.width));
      snapshot.height = static_cast<int>(std::lround(window.frame.size.height));
      snapshot.on_screen = window.isOnScreen == YES;
      return snapshot;
    }

    /**
     * @brief Build snapshots for every enumerated window.
     *
     * @param content Shareable content. May be nil.
     * @return Window snapshots.
     */
    std::vector<capture_source::window_snapshot_t> snapshots_from(SCShareableContent *content) {
      std::vector<capture_source::window_snapshot_t> snapshots;
      if (content == nil) {
        return snapshots;
      }

      snapshots.reserve(content.windows.count);
      for (SCWindow *window in content.windows) {
        snapshots.emplace_back(snapshot_window(window));
      }
      return snapshots;
    }

    /**
     * @brief Find an enumerated window by id.
     *
     * @param content Shareable content.
     * @param window_id Window id.
     * @return Retained window, or nil. The caller does not receive an extra retain; the content object owns it.
     */
    SCWindow *find_window(SCShareableContent *content, std::uint32_t window_id) {
      for (SCWindow *window in content.windows) {
        if (window.windowID == window_id) {
          return window;
        }
      }
      return nil;
    }

    /**
     * @brief Find an enumerated application by process id.
     *
     * @param content Shareable content.
     * @param pid Process id.
     * @return Application, or nil.
     */
    SCRunningApplication *find_application(SCShareableContent *content, pid_t pid) {
      for (SCRunningApplication *application in content.applications) {
        if (application.processID == pid) {
          return application;
        }
      }
      return nil;
    }

    /**
     * @brief Find the display that contains a point, preferring a configured display id.
     *
     * @param content Shareable content.
     * @param preferred_display Configured display id, or zero for any display.
     * @param point Point in desktop coordinates.
     * @return Display, or nil when ScreenCaptureKit reported none.
     */
    SCDisplay *find_display(SCShareableContent *content, CGDirectDisplayID preferred_display, CGPoint point) {
      SCDisplay *fallback = content.displays.firstObject;
      for (SCDisplay *display in content.displays) {
        if (preferred_display != 0 && display.displayID == preferred_display && CGRectContainsPoint(display.frame, point)) {
          return display;
        }
      }
      for (SCDisplay *display in content.displays) {
        if (CGRectContainsPoint(display.frame, point)) {
          return display;
        }
      }
      for (SCDisplay *display in content.displays) {
        if (preferred_display != 0 && display.displayID == preferred_display) {
          return display;
        }
      }
      return fallback;
    }

    /**
     * @brief Parse a configured macOS display id.
     *
     * @param display_name Display name from the video config.
     * @return Display id, or zero when the name is empty or not numeric.
     */
    CGDirectDisplayID parse_display_id(const std::string &display_name) {
      if (display_name.empty()) {
        return 0;
      }

      CGDirectDisplayID display_id {};
      const auto *begin = display_name.data();
      const auto *end = begin + display_name.size();
      const auto [ptr, ec] = std::from_chars(begin, end, display_id);
      if (ec != std::errc {} || ptr != end) {
        return 0;
      }
      return display_id;
    }

    /**
     * @brief Return the union of active display bounds.
     *
     * @return Desktop bounds in CoreGraphics coordinates.
     */
    CGRect active_desktop_bounds() {
      std::uint32_t display_count {};
      if (CGGetActiveDisplayList(0, nullptr, &display_count) != kCGErrorSuccess || display_count == 0) {
        return CGDisplayBounds(CGMainDisplayID());
      }

      std::vector<CGDirectDisplayID> displays(display_count);
      if (CGGetActiveDisplayList(display_count, displays.data(), &display_count) != kCGErrorSuccess || display_count == 0) {
        return CGDisplayBounds(CGMainDisplayID());
      }

      auto bounds = CGDisplayBounds(displays.front());
      for (std::uint32_t index = 1; index < display_count; ++index) {
        bounds = CGRectUnion(bounds, CGDisplayBounds(displays[index]));
      }
      return bounds;
    }

    /**
     * @brief Push float PCM from a captured application audio sample.
     *
     * @param sample_buffer Audio sample emitted by ScreenCaptureKit.
     */
    void publish_audio(CMSampleBufferRef sample_buffer) {
      const auto *description = CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sample_buffer));
      if (description == nullptr || description->mChannelsPerFrame == 0) {
        return;
      }

      const auto channels = static_cast<std::uint32_t>(description->mChannelsPerFrame);
      const auto frames = static_cast<std::size_t>(CMSampleBufferGetNumSamples(sample_buffer));
      if (frames == 0) {
        return;
      }

      const bool is_float = (description->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
      const bool non_interleaved = (description->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
      std::vector<float> interleaved(frames * channels);

      AudioBufferList *buffers = nullptr;
      size_t buffer_size = 0;
      CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample_buffer, &buffer_size, nullptr, 0, nullptr, nullptr, kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, nullptr);
      std::vector<std::uint8_t> storage(buffer_size);
      CMBlockBufferRef block = nullptr;
      if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample_buffer, nullptr, reinterpret_cast<AudioBufferList *>(storage.data()), buffer_size, nullptr, nullptr, kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr) {
        return;
      }
      buffers = reinterpret_cast<AudioBufferList *>(storage.data());

      if (is_float && description->mBitsPerChannel == 32) {
        if (!non_interleaved && buffers->mNumberBuffers > 0) {
          const auto *source = static_cast<const float *>(buffers->mBuffers[0].mData);
          const auto available = std::min(interleaved.size(), static_cast<std::size_t>(buffers->mBuffers[0].mDataByteSize / sizeof(float)));
          std::copy_n(source, available, interleaved.begin());
        } else {
          for (std::uint32_t channel = 0; channel < channels && channel < buffers->mNumberBuffers; ++channel) {
            const auto *source = static_cast<const float *>(buffers->mBuffers[channel].mData);
            for (std::size_t frame = 0; frame < frames; ++frame) {
              interleaved[frame * channels + channel] = source[frame];
            }
          }
        }
      } else if (!is_float && description->mBitsPerChannel == 16 && !non_interleaved && buffers->mNumberBuffers > 0) {
        const auto *source = static_cast<const std::int16_t *>(buffers->mBuffers[0].mData);
        const auto available = std::min(interleaved.size(), static_cast<std::size_t>(buffers->mBuffers[0].mDataByteSize / sizeof(std::int16_t)));
        for (std::size_t index = 0; index < available; ++index) {
          interleaved[index] = static_cast<float>(source[index]) / 32768.0F;
        }
      } else {
        if (block != nullptr) {
          CFRelease(block);
        }
        return;
      }

      if (block != nullptr) {
        CFRelease(block);
      }
      capture_source::push_audio(interleaved.data(), interleaved.size());
    }

    /**
     * @brief Read the frame status attached to a screen sample.
     *
     * @param sample_buffer Captured sample.
     * @return Frame status, or complete when the attachment is absent.
     */
    NSInteger frame_status(CMSampleBufferRef sample_buffer) {
      CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample_buffer, false);
      if (attachments == nullptr || CFArrayGetCount(attachments) == 0) {
        return SCFrameStatusComplete;
      }

      auto *attachment = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
      auto *status = static_cast<CFNumberRef>(CFDictionaryGetValue(attachment, (__bridge CFStringRef) SCStreamFrameInfoStatus));
      NSInteger value = SCFrameStatusComplete;
      if (status != nullptr) {
        CFNumberGetValue(status, kCFNumberNSIntegerType, &value);
      }
      return value;
    }

    /**
     * @brief Activate the captured application when remote input arrives.
     */
    void focus_captured_application() {
      const auto pid = capture_source::focus_pid();
      if (pid <= 0) {
        return;
      }

      static auto last_attempt = std::chrono::steady_clock::time_point {};
      const auto now = std::chrono::steady_clock::now();
      @autoreleasepool {
        NSRunningApplication *application = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (application == nil || application.active) {
          return;
        }
        if (now - last_attempt < 200ms) {
          return;
        }
        last_attempt = now;
        // macOS 14 ignores NSApplicationActivateIgnoringOtherApps, but older systems need it to raise the captured app.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [application activateWithOptions:NSApplicationActivateIgnoringOtherApps | NSApplicationActivateAllWindows];
#pragma clang diagnostic pop
      }
    }

    /**
     * @brief List shareable windows for the configuration UI.
     *
     * @return Windows ScreenCaptureKit can capture.
     */
    std::vector<capture_source::window_t> list_shareable_windows() {
      std::vector<capture_source::window_t> windows;
      if (!is_screen_capture_allowed()) {
        return windows;
      }

      @autoreleasepool {
        SCShareableContent *content = copy_shareable_content();
        for (const auto &snapshot : snapshots_from(content)) {
          if (snapshot.width <= 0 || snapshot.height <= 0) {
            continue;
          }
          capture_source::window_t window;
          window.window_id = snapshot.window_id;
          window.title = snapshot.title;
          window.application_id = snapshot.bundle_id;
          window.pid = snapshot.pid;
          window.width = snapshot.width;
          window.height = snapshot.height;
          if (content != nil) {
            if (auto *native = find_window(content, snapshot.window_id); native.owningApplication.applicationName != nil) {
              window.application_name = native.owningApplication.applicationName.UTF8String;
            }
          }
          windows.emplace_back(std::move(window));
        }
        [content release];
      }
      return windows;
    }

    /**
     * @brief List shareable applications for the configuration UI.
     *
     * @return Applications ScreenCaptureKit can capture.
     */
    std::vector<capture_source::application_t> list_shareable_applications() {
      std::vector<capture_source::application_t> applications;
      if (!is_screen_capture_allowed()) {
        return applications;
      }

      @autoreleasepool {
        SCShareableContent *content = copy_shareable_content();
        if (content != nil) {
          for (SCRunningApplication *application in content.applications) {
            capture_source::application_t item;
            item.pid = application.processID;
            if (application.bundleIdentifier != nil) {
              item.application_id = application.bundleIdentifier.UTF8String;
            }
            if (application.applicationName != nil) {
              item.application_name = application.applicationName.UTF8String;
            }
            if (!item.application_id.empty() || item.pid > 0) {
              applications.emplace_back(std::move(item));
            }
          }
        }
        [content release];
      }
      return applications;
    }

    /**
     * @brief Install the macOS window provider when this backend is loaded.
     */
    struct provider_registration {
      provider_registration() {
        capture_source::set_provider(list_shareable_windows, list_shareable_applications, focus_captured_application);
      }
    };

    provider_registration register_provider;  ///< Registers ScreenCaptureKit enumeration for the process lifetime.
  }  // namespace

  /**
   * @brief Forward a captured audio sample into the shared application-audio buffer.
   *
   * @param sample_buffer Audio sample emitted by ScreenCaptureKit.
   */
  void sck_publish_audio(CMSampleBufferRef sample_buffer) {
    publish_audio(sample_buffer);
  }

  /**
   * @brief Read the ScreenCaptureKit status attached to a video sample.
   *
   * @param sample_buffer Captured sample.
   * @return Frame status, or complete when the attachment is absent.
   */
  NSInteger sck_screen_frame_status(CMSampleBufferRef sample_buffer) {
    return frame_status(sample_buffer);
  }

  /**
   * @brief macOS window or application capture backed by ScreenCaptureKit.
   */
  class sck_display_t: public display_t {
  public:
    int client_width {0};  ///< Client stream width used as the maximum capture size.
    int client_height {0};  ///< Client stream height used as the maximum capture size.
    int frame_rate {60};  ///< Requested capture frame rate.
    bool ten_bit {false};  ///< Whether VideoToolbox output should be 10-bit.
    bool videotoolbox {false};  ///< Whether the encoder consumes VideoToolbox pixel buffers.
    OSType pixel_format {kCVPixelFormatType_32BGRA};  ///< Pixel format requested from ScreenCaptureKit.
    CGDirectDisplayID preferred_display {0};  ///< Display preferred for application capture.
    capture_source::match_t match {};  ///< Window or application currently being captured.
    CGFloat pixel_scale {1};  ///< Pixels per point for the captured content.
    SCKOutput *output {};  ///< Sample callback target.
    std::atomic<int> reason {static_cast<int>(stop_reason_e::none)};  ///< Why the current capture should stop.
    std::chrono::steady_clock::time_point last_geometry_check {};  ///< Last time the window list was checked for movement or closure.
    std::unique_ptr<display_device::DisplayPowerGuardInterface> display_power_guard;  ///< Keeps the display awake while capturing.

    ~sck_display_t() override {
      std::lock_guard<std::mutex> guard {stream_mutex};
      stop_stream();
      [output release];
      output = nil;
    }

    /**
     * @brief Wait until the configured window or application exists.
     *
     * @return `true` when a target was matched.
     */
    bool wait_for_target() {
      const auto target = capture_source::stream_target();
      BOOST_LOG(info) << "Waiting for "sv << capture_source::mode_name(target.mode) << " capture target"sv;

      const auto deadline = std::chrono::steady_clock::now() + 15s;
      while (std::chrono::steady_clock::now() < deadline) {
        @autoreleasepool {
          SCShareableContent *content = copy_shareable_content();
          const auto matched = capture_source::match_target(target, snapshots_from(content));
          [content release];
          if (matched) {
            match = *matched;
            CGDirectDisplayID display_id = 0;
            std::uint32_t display_count = 0;
            const CGPoint center {static_cast<CGFloat>(match.x + match.width / 2.0), static_cast<CGFloat>(match.y + match.height / 2.0)};
            if (CGGetDisplaysWithPoint(center, 1, &display_id, &display_count) == kCGErrorSuccess && display_count > 0) {
              const auto bounds = CGDisplayBounds(display_id);
              if (bounds.size.width > 0) {
                pixel_scale = static_cast<CGFloat>(CGDisplayPixelsWide(display_id)) / bounds.size.width;
              }
            }
            if (pixel_scale <= 0) {
              pixel_scale = 1;
            }
            fit_output(
              static_cast<int>(std::lround(match.width * pixel_scale)),
              static_cast<int>(std::lround(match.height * pixel_scale)),
              client_width,
              client_height,
              width,
              height
            );
            capture_source::set_focus_pid(match.pid);
            capture_source::set_live_geometry({match.x, match.y, match.width, match.height});
            BOOST_LOG(info) << "Capturing "sv << (match.application ? "application ["sv : "window ["sv)
                            << match.bundle_id << "] pid "sv << match.pid
                            << " at "sv << match.width << 'x' << match.height << " origin "sv << match.x << 'x' << match.y;
            return true;
          }
        }
        std::this_thread::sleep_for(200ms);
      }

      BOOST_LOG(error) << "The capture target did not appear. The session will end without capturing the display."sv;
      return false;
    }

    /**
     * @brief Refresh window geometry and report whether capture should stop or restart.
     *
     * @return `false` when the target disappeared or its pixel size changed.
     */
    bool refresh_geometry() {
      const auto now = std::chrono::steady_clock::now();
      if (now - last_geometry_check < 500ms) {
        return true;
      }
      last_geometry_check = now;

      const auto target = capture_source::stream_target();
      @autoreleasepool {
        SCShareableContent *content = copy_shareable_content();
        const auto matched = capture_source::match_target(target, snapshots_from(content));
        [content release];
        if (!matched) {
          BOOST_LOG(info) << "The capture target is no longer available. Ending the session."sv;
          reason.store(static_cast<int>(stop_reason_e::interrupted));
          return false;
        }

        capture_source::set_live_geometry({matched->x, matched->y, matched->width, matched->height});
        capture_source::set_focus_pid(matched->pid);
        int output_width = 0;
        int output_height = 0;
        fit_output(
          static_cast<int>(std::lround(matched->width * pixel_scale)),
          static_cast<int>(std::lround(matched->height * pixel_scale)),
          client_width,
          client_height,
          output_width,
          output_height
        );
        if (output_width != width || output_height != height) {
          BOOST_LOG(info) << "Capture target size changed from "sv << width << 'x' << height << " to "sv << output_width << 'x' << output_height << ". Updating the stream resolution."sv;
          reason.store(static_cast<int>(stop_reason_e::reinit));
          return false;
        }
        match = *matched;
      }
      return true;
    }

    /**
     * @brief Create the content filter for the matched target.
     *
     * @param content Fresh shareable-content snapshot.
     * @return A retained filter, or nil when the target disappeared.
     */
    SCContentFilter *create_filter(SCShareableContent *content) {
      if (content == nil) {
        return nil;
      }

      if (!match.application) {
        SCWindow *window = find_window(content, match.window_id);
        if (window == nil) {
          return nil;
        }
        return [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
      }

      SCRunningApplication *application = find_application(content, match.pid);
      if (application == nil && !match.bundle_id.empty()) {
        for (SCRunningApplication *candidate in content.applications) {
          if (candidate.bundleIdentifier != nil && match.bundle_id == candidate.bundleIdentifier.UTF8String) {
            application = candidate;
            break;
          }
        }
      }
      if (application == nil) {
        return nil;
      }

      const CGPoint center {static_cast<CGFloat>(match.x + match.width / 2.0), static_cast<CGFloat>(match.y + match.height / 2.0)};
      SCDisplay *display = find_display(content, preferred_display, center);
      if (display == nil) {
        return nil;
      }
      return [[SCContentFilter alloc] initWithDisplay:display includingApplications:@[application] exceptingWindows:@[]];
    }

    /**
     * @brief Start a stream and wait until the caller stops it.
     *
     * @param on_video Called for each video sample. Returning `false` stops capture.
     * @return Capture status for the streaming pipeline.
     */
    capture_e run_stream(const std::function<bool(CMSampleBufferRef)> &on_video) {
      std::lock_guard<std::mutex> guard {stream_mutex};
      reason.store(static_cast<int>(stop_reason_e::none));
      @autoreleasepool {
        SCShareableContent *content = copy_shareable_content();
        SCContentFilter *filter = create_filter(content);
        if (filter == nil) {
          [content release];
          BOOST_LOG(error) << "The capture target is no longer available. Ending the session."sv;
          return capture_e::interrupted;
        }

        if (@available(macOS 14.0, *)) {
          SCShareableContentInfo *info = [SCShareableContent infoForFilter:filter];
          if (info.pointPixelScale > 0) {
            pixel_scale = info.pointPixelScale;
          }
        }
        int output_width = 0;
        int output_height = 0;
        fit_output(
          static_cast<int>(std::lround(match.width * pixel_scale)),
          static_cast<int>(std::lround(match.height * pixel_scale)),
          client_width,
          client_height,
          output_width,
          output_height
        );
        width = output_width;
        height = output_height;
        cursor_applied = shows_cursor();

        auto *configuration = [[SCStreamConfiguration alloc] init];
        configuration.width = static_cast<size_t>(width);
        configuration.height = static_cast<size_t>(height);
        configuration.pixelFormat = pixel_format;
        configuration.minimumFrameInterval = CMTimeMake(1, std::max(frame_rate, 1));
        configuration.queueDepth = 3;
        configuration.scalesToFit = YES;
        configuration.showsCursor = cursor_applied ? YES : NO;
        static CGColorRef background = CGColorCreateGenericRGB(0, 0, 0, 1);
        configuration.backgroundColor = background;
        if (@available(macOS 14.0, *)) {
          configuration.preservesAspectRatio = YES;
          configuration.ignoreShadowsSingleWindow = YES;
        }

        const auto audio = capture_source::audio_request();
        const bool capture_audio = audio.sample_rate > 0 && audio.channels > 0;
        if (capture_audio) {
          if (@available(macOS 13.0, *)) {
            configuration.capturesAudio = YES;
            configuration.sampleRate = audio.sample_rate;
            configuration.channelCount = audio.channels;
            configuration.excludesCurrentProcessAudio = YES;
          }
        }

        if (output == nil) {
          output = [[SCKOutput alloc] init];
          output->reason = &reason;
        }
        video_fn = on_video;
        output->on_video = &video_fn;
        if (output->signal != nullptr) {
          dispatch_release(output->signal);
          output->signal = nullptr;
        }
        output->signal = dispatch_semaphore_create(0);
        if (output->signal == nullptr) {
          BOOST_LOG(error) << "Unable to create the window capture signal."sv;
          return capture_e::capture_error;
        }

        NSError *stream_error = nil;
        output.stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:output];
        [configuration release];
        [filter release];
        [content release];

        if (output.stream == nil) {
          dispatch_release(output->signal);
          output->signal = nullptr;
          BOOST_LOG(error) << "Unable to create the window capture stream."sv;
          return capture_e::capture_error;
        }

        if (![output.stream addStreamOutput:output type:SCStreamOutputTypeScreen sampleHandlerQueue:dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0) error:&stream_error]) {
          BOOST_LOG(error) << "Unable to receive window capture frames: "sv << stream_error.localizedDescription.UTF8String;
          [output.stream release];
          output.stream = nil;
          dispatch_release(output->signal);
          output->signal = nullptr;
          return capture_e::capture_error;
        }

        if (capture_audio) {
          if (@available(macOS 13.0, *)) {
            NSError *audio_error = nil;
            if ([output.stream addStreamOutput:output type:SCStreamOutputTypeAudio sampleHandlerQueue:dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0) error:&audio_error]) {
              capture_source::open_audio(audio);
            } else {
              BOOST_LOG(warning) << "Application audio is unavailable for this capture target: "sv << audio_error.localizedDescription.UTF8String;
            }
          }
        }

        auto *started = dispatch_semaphore_create(0);
        __block NSError *start_error = nil;
        [output.stream startCaptureWithCompletionHandler:^(NSError *capture_error) {
          start_error = [capture_error retain];
          dispatch_semaphore_signal(started);
        }];
        dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);
        dispatch_release(started);
        if (start_error != nil) {
          BOOST_LOG(error) << "Window capture failed to start: "sv << start_error.localizedDescription.UTF8String;
          [start_error release];
          stop_stream();
          return capture_e::capture_error;
        }

        if (output->signal == nullptr) {
          stop_stream();
          return capture_e::capture_error;
        }
        dispatch_semaphore_wait(output->signal, DISPATCH_TIME_FOREVER);
        const auto stop = static_cast<stop_reason_e>(reason.load());
        stop_stream();
        if (stop == stop_reason_e::reinit) {
          return capture_e::reinit;
        }
        if (stop == stop_reason_e::error) {
          return capture_e::capture_error;
        }
        return capture_e::interrupted;
      }
    }

    /**
     * @brief Stop the stream and release its callbacks.
     */
    void stop_stream() {
      capture_source::close_audio();
      if (output == nil || output.stream == nil) {
        return;
      }

      auto *stopped = dispatch_semaphore_create(0);
      [output.stream stopCaptureWithCompletionHandler:^(NSError *) {
        dispatch_semaphore_signal(stopped);
      }];
      dispatch_semaphore_wait(stopped, dispatch_time(DISPATCH_TIME_NOW, 2LL * NSEC_PER_SEC));
      dispatch_release(stopped);
      [output.stream release];
      output.stream = nil;
      output->on_video = nullptr;
      if (output->signal != nullptr) {
        dispatch_release(output->signal);
        output->signal = nullptr;
      }
    }

    /**
     * @brief Apply a cursor-visibility change without recreating the stream.
     */
    void update_cursor() {
      const bool show = shows_cursor();
      if (show == cursor_applied || output == nil || output.stream == nil) {
        return;
      }

      cursor_applied = show;
      auto *configuration = [[SCStreamConfiguration alloc] init];
      configuration.width = static_cast<size_t>(std::max(width, 2));
      configuration.height = static_cast<size_t>(std::max(height, 2));
      configuration.pixelFormat = pixel_format;
      configuration.minimumFrameInterval = CMTimeMake(1, std::max(frame_rate, 1));
      configuration.scalesToFit = YES;
      configuration.showsCursor = show ? YES : NO;
      if (@available(macOS 14.0, *)) {
        configuration.preservesAspectRatio = YES;
      }
      [output.stream updateConfiguration:configuration
                       completionHandler:^(NSError *error) {
                         if (error != nil) {
                           BOOST_LOG(debug) << "Unable to update cursor capture: "sv << error.localizedDescription.UTF8String;
                         }
                       }];
      [configuration release];
    }

    /**
     * @brief Report whether the cursor should be drawn into the capture.
     *
     * @return Cursor visibility for the current target.
     */
    [[nodiscard]] bool shows_cursor() const {
      const auto target = capture_source::stream_target();
      if (target.show_cursor) {
        return *target.show_cursor;
      }
      return cursor_visible == nullptr || *cursor_visible;
    }

    bool *cursor_visible {nullptr};  ///< Session cursor toggle. The application setting replaces it when present.
    bool cursor_applied {true};  ///< Cursor visibility last sent to ScreenCaptureKit.
    std::function<bool(CMSampleBufferRef)> video_fn;  ///< Video callback used by the current stream.
    std::mutex stream_mutex;  ///< Keeps encoder setup from opening a second stream.

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      cursor_visible = cursor;
      return run_stream([&](CMSampleBufferRef sample) {
        if (!refresh_geometry()) {
          return false;
        }
        update_cursor();
        return publish_sample(sample, pull_free_image_cb, push_captured_image_cb);
      });
    }

    std::shared_ptr<img_t> alloc_img() override {
      return std::make_shared<av_img_t>();
    }

    std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e pix_fmt) override {
      if (pix_fmt == pix_fmt_e::yuv420p) {
        pixel_format = kCVPixelFormatType_32BGRA;
        return std::make_unique<avcodec_encode_device_t>();
      }
      if (pix_fmt == pix_fmt_e::nv12 || pix_fmt == pix_fmt_e::p010) {
        pixel_format = pix_fmt == pix_fmt_e::p010 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        auto device = std::make_unique<nv12_zero_device>();
        device->init(
          pix_fmt,
          [](int, int) {
          },
          [this](int format) {
            pixel_format = static_cast<OSType>(format);
          }
        );
        return device;
      }

      BOOST_LOG(error) << "Unsupported pixel format for window capture."sv;
      return nullptr;
    }

    int dummy_img(img_t *img) override {
      if (!is_screen_capture_allowed()) {
        return 1;
      }

      auto *image = static_cast<av_img_t *>(img);
      CMSampleBufferRef sample = blank_sample(std::max(width, 2), std::max(height, 2), pixel_format);
      if (sample == nullptr) {
        return 1;
      }

      auto new_sample_buffer = std::make_shared<av_sample_buf_t>(sample);
      CFRelease(sample);
      auto new_pixel_buffer = std::make_shared<av_pixel_buf_t>(new_sample_buffer->buf);
      if (new_pixel_buffer->buf == nullptr) {
        return 1;
      }

      auto old_data_retainer = std::make_shared<temp_retain_av_img_t>(image->sample_buffer, image->pixel_buffer, image->data);
      image->sample_buffer = new_sample_buffer;
      image->pixel_buffer = new_pixel_buffer;
      image->data = new_pixel_buffer->data();
      image->width = static_cast<int>(CVPixelBufferGetWidth(new_pixel_buffer->buf));
      image->height = static_cast<int>(CVPixelBufferGetHeight(new_pixel_buffer->buf));
      image->row_pitch = static_cast<int>(CVPixelBufferGetBytesPerRow(new_pixel_buffer->buf));
      image->pixel_pitch = image->width > 0 ? image->row_pitch / image->width : 0;
      old_data_retainer.reset();
      return 0;
    }
  };

  std::shared_ptr<display_t> sck_display(mem_type_e hwdevice_type, const std::string &display_name, int width, int height, int framerate, bool ten_bit) {
    if (!is_screen_capture_allowed()) {
      BOOST_LOG(error) << "Screen recording permission is required for window and application capture."sv;
      return nullptr;
    }

    auto display = std::make_shared<sck_display_t>();
    display->display_power_guard = display_device::keep_display_awake("Sunshine window capture");
    display->client_width = width;
    display->client_height = height;
    display->frame_rate = framerate > 0 ? framerate : 60;
    display->ten_bit = ten_bit;
    display->videotoolbox = hwdevice_type == mem_type_e::videotoolbox;
    display->preferred_display = parse_display_id(display_name);
    if (display->videotoolbox) {
      display->pixel_format = ten_bit ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    }

    if (!display->wait_for_target()) {
      return nullptr;
    }

    display->logical_width = std::max(display->match.width, 1);
    display->logical_height = std::max(display->match.height, 1);
    display->offset_x = display->match.x;
    display->offset_y = display->match.y;

    const auto desktop = active_desktop_bounds();
    display->env_offset_x = static_cast<int>(desktop.origin.x);
    display->env_offset_y = static_cast<int>(desktop.origin.y);
    display->env_logical_width = static_cast<int>(desktop.size.width);
    display->env_logical_height = static_cast<int>(desktop.size.height);
    display->env_width = display->env_logical_width;
    display->env_height = display->env_logical_height;
    return display;
  }
}  // namespace platf

@implementation SCKOutput

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
  (void) stream;
  if (self->reason != nullptr && self->reason->load() != static_cast<int>(platf::stop_reason_e::none)) {
    return;
  }

  if (type == SCStreamOutputTypeAudio) {
    platf::sck_publish_audio(sampleBuffer);
    return;
  }
  if (type != SCStreamOutputTypeScreen || self->on_video == nullptr) {
    return;
  }

  const auto status = platf::sck_screen_frame_status(sampleBuffer);
  if (status == SCFrameStatusIdle || status == SCFrameStatusBlank || status == SCFrameStatusSuspended) {
    return;
  }
  if (status == SCFrameStatusStopped) {
    if (self->reason != nullptr) {
      self->reason->store(static_cast<int>(platf::stop_reason_e::interrupted));
    }
    if (self->signal != nullptr) {
      dispatch_semaphore_signal(self->signal);
    }
    return;
  }
  if (CMSampleBufferGetImageBuffer(sampleBuffer) == nullptr) {
    return;
  }
  if (!(*self->on_video)(sampleBuffer) && self->signal != nullptr) {
    dispatch_semaphore_signal(self->signal);
  }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  (void) stream;
  using namespace std::literals;
  BOOST_LOG(info) << "Window capture stopped: "sv << (error.localizedDescription == nil ? "the target is no longer available" : error.localizedDescription.UTF8String);
  if (self->reason != nullptr) {
    self->reason->store(static_cast<int>(platf::stop_reason_e::interrupted));
  }
  if (self->signal != nullptr) {
    dispatch_semaphore_signal(self->signal);
  }
}

- (void)streamDidBecomeInactive:(SCStream *)stream API_AVAILABLE(macos(15.2)) {
  (void) stream;
  using namespace std::literals;
  BOOST_LOG(info) << "The capture target is no longer available. Ending the session."sv;
  if (self->reason != nullptr) {
    self->reason->store(static_cast<int>(platf::stop_reason_e::interrupted));
  }
  if (self->signal != nullptr) {
    dispatch_semaphore_signal(self->signal);
  }
}

@end
