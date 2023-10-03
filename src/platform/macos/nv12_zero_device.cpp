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

  int
  nv12_zero_device::convert(platf::img_t &img) {
    av_frame_make_writable(av_frame.get());

    av_img_t *av_img = (av_img_t *) &img;

    // Set up the data fields in the AVFrame to point into the mapped CVPixelBuffer
    int planes = CVPixelBufferGetPlaneCount(av_img->pixel_buffer->buf);
    for (int i = 0; i < planes; i++) {
      av_frame->linesize[i] = CVPixelBufferGetBytesPerRowOfPlane(av_img->pixel_buffer->buf, i);
      av_frame->data[i] = (uint8_t *) CVPixelBufferGetBaseAddressOfPlane(av_img->pixel_buffer->buf, i);
    }

    // We just set data pointers to point into our CVPixelBuffer above, so we have to hold
    // a reference to these buffers to keep them around until the AVFrame is done using them.
    backing_img.sample_buffer = av_img->sample_buffer;
    backing_img.pixel_buffer = av_img->pixel_buffer;

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
  nv12_zero_device::init(void *display, resolution_fn_t resolution_fn, pixel_format_fn_t pixel_format_fn) {
    pixel_format_fn(display, '420v');

    this->display = display;
    this->resolution_fn = resolution_fn;

    // we never use this pointer but it's existence is checked/used
    // by the platform independent code
    data = this;

    return 0;
  }

}  // namespace platf
