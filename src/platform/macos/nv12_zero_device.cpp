#include "src/platform/macos/nv12_zero_device.h"
#include "src/platform/macos/av_img_t.h"

#include "src/video.h"

extern "C" {
#include "libavutil/imgutils.h"
}

namespace platf {

void free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}

util::safe_ptr<AVFrame, free_frame> av_frame;

int nv12_zero_device::convert(platf::img_t &img) {
  av_frame_make_writable(av_frame.get());

  av_img_t *av_img = (av_img_t *)&img;

  size_t left_pad, right_pad, top_pad, bottom_pad;
  CVPixelBufferGetExtendedPixels(av_img->pixel_buffer, &left_pad, &right_pad, &top_pad, &bottom_pad);

  const uint8_t *data = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(av_img->pixel_buffer, 0) - left_pad - (top_pad * img.width);

  int result = av_image_fill_arrays(av_frame->data, av_frame->linesize, data, (AVPixelFormat)av_frame->format, img.width, img.height, 32);

  // We will create the black bars for the padding top/bottom or left/right here in very cheap way.
  // The luminance is 0, therefore, we simply need to set the chroma values to 128 for each pixel
  // for black bars (instead of green with chroma 0). However, this only works 100% correct, when
  // the resolution is devisable by 32. This could be improved by calculating the chroma values for
  // the outer content pixels, which should introduce only a minor performance hit.
  //
  // XXX: Improve the algorithm to take into account the outer pixels

  size_t uv_plane_height = CVPixelBufferGetHeightOfPlane(av_img->pixel_buffer, 1);

  if(left_pad || right_pad) {
    for(int l = 0; l < uv_plane_height + (top_pad / 2); l++) {
      int line = l * av_frame->linesize[1];
      memset((void *)&av_frame->data[1][line], 128, (size_t)left_pad);
      memset((void *)&av_frame->data[1][line + img.width - right_pad], 128, right_pad);
    }
  }

  if(top_pad || bottom_pad) {
    memset((void *)&av_frame->data[1][0], 128, (top_pad / 2) * av_frame->linesize[1]);
    memset((void *)&av_frame->data[1][((top_pad / 2) + uv_plane_height) * av_frame->linesize[1]], 128, bottom_pad / 2 * av_frame->linesize[1]);
  }

  return result > 0 ? 0 : -1;
}

int nv12_zero_device::set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) {
  this->frame = frame;

  av_frame.reset(frame);

  resolution_fn(this->display, frame->width, frame->height);

  return 0;
}

void nv12_zero_device::set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) {
}

int nv12_zero_device::init(void *display, resolution_fn_t resolution_fn, pixel_format_fn_t pixel_format_fn) {
  pixel_format_fn(display, '420v');

  this->display       = display;
  this->resolution_fn = resolution_fn;

  // we never use this pointer but it's existence is checked/used
  // by the platform independed code
  data = this;

  return 0;
}

} // namespace platf
