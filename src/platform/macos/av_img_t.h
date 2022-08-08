#ifndef av_img_t_h
#define av_img_t_h

#include "src/platform/common.h"

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>

namespace platf {
struct av_img_t : public img_t {
  CVPixelBufferRef pixel_buffer   = nullptr;
  CMSampleBufferRef sample_buffer = nullptr;

  ~av_img_t();
};
} // namespace platf

#endif /* av_img_t_h */
