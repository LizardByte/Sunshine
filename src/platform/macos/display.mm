/**
 * @file src/platform/macos/display.mm
 * @brief todo
 */
#include "src/platform/common.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/av_video.h"
#include "src/platform/macos/nv12_zero_device.h"

#include "src/config.h"
#include "src/main.h"

// Avoid conflict between AVFoundation and libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "src/video.h"
#undef AVMediaType

namespace fs = std::filesystem;

namespace platf {
  using namespace std::literals;

  struct av_display_t: public display_t {
    AVVideo *av_capture;
    CGDirectDisplayID display_id;

    ~av_display_t() {
      [av_capture release];
    }

    capture_e
    capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto signal = [av_capture capture:^(CMSampleBufferRef sampleBuffer) {
        std::shared_ptr<img_t> img_out;
        if (!pull_free_image_cb(img_out)) {
          // got interrupt signal
          // returning false here stops capture backend
          return false;
        }
        auto av_img = std::static_pointer_cast<av_img_t>(img_out);

        CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);

        av_img->sample_buffer = std::make_shared<av_sample_buf_t>(sampleBuffer);
        av_img->pixel_buffer = std::make_shared<av_pixel_buf_t>(pixelBuffer);
        img_out->data = av_img->pixel_buffer->lock();

        img_out->width = CVPixelBufferGetWidth(pixelBuffer);
        img_out->height = CVPixelBufferGetHeight(pixelBuffer);
        img_out->row_pitch = CVPixelBufferGetBytesPerRow(pixelBuffer);
        img_out->pixel_pitch = img_out->row_pitch / img_out->width;

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

    std::shared_ptr<img_t>
    alloc_img() override {
      return std::make_shared<av_img_t>();
    }

    std::unique_ptr<avcodec_encode_device_t>
    make_avcodec_encode_device(pix_fmt_e pix_fmt) override {
      if (pix_fmt == pix_fmt_e::yuv420p) {
        av_capture.pixelFormat = kCVPixelFormatType_32BGRA;

        return std::make_unique<avcodec_encode_device_t>();
      }
      else if (pix_fmt == pix_fmt_e::nv12) {
        auto device = std::make_unique<nv12_zero_device>();

        device->init(static_cast<void *>(av_capture), setResolution, setPixelFormat);

        return device;
      }
      else {
        BOOST_LOG(error) << "Unsupported Pixel Format."sv;
        return nullptr;
      }
    }

    int
    dummy_img(img_t *img) override {
      auto signal = [av_capture capture:^(CMSampleBufferRef sampleBuffer) {
        auto av_img = (av_img_t *) img;

        CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);

        av_img->sample_buffer = std::make_shared<av_sample_buf_t>(sampleBuffer);
        av_img->pixel_buffer = std::make_shared<av_pixel_buf_t>(pixelBuffer);
        img->data = av_img->pixel_buffer->lock();

        img->width = CVPixelBufferGetWidth(pixelBuffer);
        img->height = CVPixelBufferGetHeight(pixelBuffer);
        img->row_pitch = CVPixelBufferGetBytesPerRow(pixelBuffer);
        img->pixel_pitch = img->row_pitch / img->width;

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
    static void
    setResolution(void *display, int width, int height) {
      [static_cast<AVVideo *>(display) setFrameWidth:width frameHeight:height];
    }

    static void
    setPixelFormat(void *display, OSType pixelFormat) {
      static_cast<AVVideo *>(display).pixelFormat = pixelFormat;
    }
  };

  std::shared_ptr<display_t>
  display(platf::mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system) {
      BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    auto display = std::make_shared<av_display_t>();

    display->display_id = CGMainDisplayID();
    if (!display_name.empty()) {
      auto display_array = [AVVideo displayNames];

      for (NSDictionary *item in display_array) {
        NSString *name = item[@"name"];
        if (name.UTF8String == display_name) {
          NSNumber *display_id = item[@"id"];
          display->display_id = [display_id unsignedIntValue];
        }
      }
    }

    display->av_capture = [[AVVideo alloc] initWithDisplay:display->display_id frameRate:config.framerate];

    if (!display->av_capture) {
      BOOST_LOG(error) << "Video setup failed."sv;
      return nullptr;
    }

    display->width = display->av_capture.frameWidth;
    display->height = display->av_capture.frameHeight;

    return display;
  }

  std::vector<std::string>
  display_names(mem_type_e hwdevice_type) {
    __block std::vector<std::string> display_names;

    auto display_array = [AVVideo displayNames];

    display_names.reserve([display_array count]);
    [display_array enumerateObjectsUsingBlock:^(NSDictionary *_Nonnull obj, NSUInteger idx, BOOL *_Nonnull stop) {
      NSString *name = obj[@"name"];
      display_names.push_back(name.UTF8String);
    }];

    return display_names;
  }
}  // namespace platf
