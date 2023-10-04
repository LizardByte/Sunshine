/**
 * @file src/platform/macos/nv12_zero_device.cpp
 * @brief todo
 */
#include "src/platform/macos/nv12_zero_device.h"

#include "src/video.h"

extern "C" {
#include "libavutil/imgutils.h"
}

namespace platf {

  void
  free_frame(AVFrame *frame) {
    av_frame_free(&frame);
  }

  void
  free_buffer(void *opaque, uint8_t *data) {
    CVPixelBufferRelease((CVPixelBufferRef) data);
  }

  int
  nv12_zero_device::convert(platf::img_t &img) {
    av_img_t *av_img = (av_img_t *) &img;

    av_buffer_unref(&av_frame->buf[0]);

    av_frame->buf[0] = av_buffer_create((uint8_t *) CFRetain(av_img->pixel_buffer->buf), 0, free_buffer, NULL, 0);
    av_frame->data[3] = (uint8_t *) av_img->pixel_buffer->buf;

    return 0;
  }

  int
  nv12_zero_device::set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) {
    this->frame = frame;

    av_frame.reset(frame);

    resolution_fn(this->display, frame->width, frame->height);

    return 0;
  }

  int
  nv12_zero_device::init(void *display, pix_fmt_e pix_fmt, resolution_fn_t resolution_fn, pixel_format_fn_t pixel_format_fn) {
    pixel_format_fn(display, pix_fmt == pix_fmt_e::nv12 ?
                               kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange :
                               kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);

    this->display = display;
    this->resolution_fn = resolution_fn;

    // we never use this pointer but it's existence is checked/used
    // by the platform independent code
    data = this;

    return 0;
  }

}  // namespace platf
