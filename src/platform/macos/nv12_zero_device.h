/**
 * @file src/platform/macos/nv12_zero_device.h
 * @brief Declarations for NV12 zero copy device on macOS.
 */
#pragma once

// local includes
#include "src/platform/common.h"

// standard includes
#include <unordered_set>

// platform includes
#include <VideoToolbox/VideoToolbox.h>

struct AVFrame;

namespace platf {
  void free_frame(AVFrame *frame);

  class nv12_zero_device: public avcodec_encode_device_t {
    // display holds a pointer to an av_video object. Since the namespaces of AVFoundation
    // and FFMPEG collide, we need this opaque pointer and cannot use the definition
    void *display;

  public:
    // this function is used to set the resolution on an av_video object that we cannot
    // call directly because of namespace collisions between AVFoundation and FFMPEG
    using resolution_fn_t = std::function<void(void *display, int width, int height)>;
    resolution_fn_t resolution_fn;
    using pixel_format_fn_t = std::function<void(void *display, int pixelFormat)>;

    ~nv12_zero_device() override;

    int init(void *display, pix_fmt_e pix_fmt, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn, bool resize_capture = true);

    int convert(img_t &img) override;
    int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override;

  private:
    util::safe_ptr<AVFrame, free_frame> av_frame;
    VTPixelTransferSessionRef transfer_session {};
    CVPixelBufferPoolRef pixel_buffer_pool {};
    OSType output_pixel_format {};
    OSType pool_pixel_format {};
    int pool_width {};
    int pool_height {};
    bool resize_capture {};
    std::unordered_set<const void *> black_surfaces;

    int attach_pixel_buffer(CVPixelBufferRef pixel_buffer);
    CVPixelBufferRef copy_scaled_pixel_buffer(CVPixelBufferRef pixel_buffer);
    int ensure_pixel_buffer_pool(OSType pixel_format);
    int ensure_transfer_session();
    int prefill_black(CVPixelBufferRef pixel_buffer);
  };

}  // namespace platf
