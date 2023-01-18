#ifndef vtdevice_h
#define vtdevice_h

#include "src/platform/common.h"

namespace platf {

class nv12_zero_device : public hwdevice_t {
  // display holds a pointer to an av_video object. Since the namespaces of AVFoundation
  // and FFMPEG collide, we need this opaque pointer and cannot use the definition
  void *display;

public:
  // this function is used to set the resolution on an av_video object that we cannot
  // call directly because of namespace collisions between AVFoundation and FFMPEG
  using resolution_fn_t = std::function<void(void *display, int width, int height)>;
  resolution_fn_t resolution_fn;
  using pixel_format_fn_t = std::function<void(void *display, int pixelFormat)>;

  int init(void *display, resolution_fn_t resolution_fn, pixel_format_fn_t pixel_format_fn);

  int convert(img_t &img);
  int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx);
  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range);
};

} // namespace platf

#endif /* vtdevice_h */
