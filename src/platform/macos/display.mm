/**
 * @file src/platform/macos/display.mm
 * @brief Definitions for display capture on macOS.
 */
// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/misc.h"
#include "src/platform/macos/nv12_zero_device.h"
#include "src/platform/macos/sckit_video.h"

// Avoid conflict between AVFoundation and libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "src/video.h"
#undef AVMediaType

namespace fs = std::filesystem;

namespace platf {
  using namespace std::literals;

  struct av_display_t: public display_t {
    SCKitVideo *capture_backend {};
    CGDirectDisplayID display_id {};

    ~av_display_t() override {
      [capture_backend release];
    }

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto signal = [capture_backend capture:^(CMSampleBufferRef sampleBuffer) {
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
        img_out->frame_timestamp = std::chrono::steady_clock::now();

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
        capture_backend.pixelFormat = kCVPixelFormatType_32BGRA;

        return std::make_unique<avcodec_encode_device_t>();
      } else if (pix_fmt == pix_fmt_e::nv12 || pix_fmt == pix_fmt_e::p010) {
        auto device = std::make_unique<nv12_zero_device>();

        device->init(static_cast<void *>(capture_backend), pix_fmt, setResolution, setPixelFormat);

        return device;
      } else {
        BOOST_LOG(error) << "Unsupported Pixel Format."sv;
        return nullptr;
      }
    }

    int dummy_img(img_t *img) override {
      auto av_img = (av_img_t *) img;
      const auto width = capture_backend.frameWidth;
      const auto height = capture_backend.frameHeight;
      const auto pixel_format = capture_backend.pixelFormat;

      CVPixelBufferRef pixel_buffer = nullptr;
      NSDictionary *attributes = @{
        (id) kCVPixelBufferIOSurfacePropertiesKey: @{}
      };
      auto status = CVPixelBufferCreate(
        kCFAllocatorDefault,
        width,
        height,
        pixel_format,
        (CFDictionaryRef) attributes,
        &pixel_buffer
      );
      if (status != kCVReturnSuccess || pixel_buffer == nullptr) {
        BOOST_LOG(error) << "Failed to allocate macOS dummy pixel buffer: " << status;
        return 1;
      }

      CVPixelBufferLockBaseAddress(pixel_buffer, 0);
      if (CVPixelBufferIsPlanar(pixel_buffer)) {
        for (size_t plane = 0; plane < CVPixelBufferGetPlaneCount(pixel_buffer); ++plane) {
          auto base = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, plane));
          auto bytes_per_row = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, plane);
          auto plane_height = CVPixelBufferGetHeightOfPlane(pixel_buffer, plane);
          memset(base, plane == 0 ? 0x00 : 0x80, bytes_per_row * plane_height);
        }
      } else {
        auto base = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(pixel_buffer));
        auto bytes_per_row = CVPixelBufferGetBytesPerRow(pixel_buffer);
        memset(base, 0x00, bytes_per_row * height);
      }
      CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);

      CMVideoFormatDescriptionRef format_description = nullptr;
      auto cm_status = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixel_buffer, &format_description);
      if (cm_status != noErr || format_description == nullptr) {
        BOOST_LOG(error) << "Failed to create macOS dummy video format description: " << cm_status;
        CVPixelBufferRelease(pixel_buffer);
        return 1;
      }

      CMSampleTimingInfo timing_info {};
      timing_info.duration = kCMTimeInvalid;
      timing_info.presentationTimeStamp = kCMTimeZero;
      timing_info.decodeTimeStamp = kCMTimeInvalid;

      CMSampleBufferRef sample_buffer = nullptr;
      cm_status = CMSampleBufferCreateReadyWithImageBuffer(
        kCFAllocatorDefault,
        pixel_buffer,
        format_description,
        &timing_info,
        &sample_buffer
      );
      CFRelease(format_description);
      CVPixelBufferRelease(pixel_buffer);

      if (cm_status != noErr || sample_buffer == nullptr) {
        BOOST_LOG(error) << "Failed to create macOS dummy sample buffer: " << cm_status;
        return 1;
      }

      auto new_sample_buffer = std::make_shared<av_sample_buf_t>(sample_buffer);
      auto new_pixel_buffer = std::make_shared<av_pixel_buf_t>(new_sample_buffer->buf);
      CFRelease(sample_buffer);

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

      old_data_retainer = nullptr;

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
      BOOST_LOG(info) << "ScreenCaptureKit encoder requested capture size " << width << "x" << height;
      [static_cast<SCKitVideo *>(display) setFrameWidth:width frameHeight:height];
    }

    static void setPixelFormat(void *display, OSType pixelFormat) {
      static_cast<SCKitVideo *>(display).pixelFormat = pixelFormat;
    }
  };

  std::shared_ptr<display_t> display(platf::mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::videotoolbox) {
      BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    auto display = std::make_shared<av_display_t>();

    // Default to main display
    display->display_id = CGMainDisplayID();

    // Print all displays available with it's name and id
    auto display_array = [SCKitVideo displayNames];
    BOOST_LOG(info) << "Detecting displays"sv;
    for (NSDictionary *item in display_array) {
      NSNumber *display_id = item[@"id"];
      // We need show display's product name and corresponding display number given by user
      NSString *name = item[@"displayName"];
      // We are using CGGetActiveDisplayList that only returns active displays so hardcoded connected value in log to true
      BOOST_LOG(info) << "Detected display: "sv << name.UTF8String << " (id: "sv << [NSString stringWithFormat:@"%@", display_id].UTF8String << ") connected: true"sv;
      if (!display_name.empty() && std::atoi(display_name.c_str()) == [display_id unsignedIntValue]) {
        display->display_id = [display_id unsignedIntValue];
      }
    }
    BOOST_LOG(info) << "Configuring selected display ("sv << display->display_id << ") to stream"sv;

    display->capture_backend = [[SCKitVideo alloc] initWithDisplay:display->display_id frameRate:config.framerate];

    if (!display->capture_backend) {
      BOOST_LOG(error) << "Video setup failed."sv;
      return nullptr;
    }

    display->width = display->capture_backend.frameWidth;
    display->height = display->capture_backend.frameHeight;
    // We also need set env_width and env_height for absolute mouse coordinates
    display->env_width = display->width;
    display->env_height = display->height;

    if (config.width > 0 && config.height > 0) {
      BOOST_LOG(info) << "ScreenCaptureKit capture target for display " << display->display_id
                      << " source=" << display->width << "x" << display->height
                      << " client=" << config.width << "x" << config.height;
      [display->capture_backend setFrameWidth:config.width frameHeight:config.height];
    }

    return display;
  }

  std::vector<std::string> display_names(mem_type_e hwdevice_type) {
    __block std::vector<std::string> display_names;

    auto display_array = [SCKitVideo displayNames];

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
