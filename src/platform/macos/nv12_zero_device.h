/**
 * @file src/platform/macos/nv12_zero_device.h
 * @brief Declarations for NV12 zero copy device on macOS.
 */
#pragma once

#include "src/platform/common.h"

struct AVFrame;

namespace platf {
  void
  free_frame(AVFrame *frame);

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

    int
    init(void *display, pix_fmt_e pix_fmt, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn);

    int
    convert(img_t &img) override;
    int
    set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override;

  private:
    util::safe_ptr<AVFrame, free_frame> av_frame;
  };

}  // namespace platf
