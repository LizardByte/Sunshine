#ifndef SUNSHINE_PLATFORM_CUDA_H
#define SUNSHINE_PLATFORM_CUDA_H

#include <memory>

#ifndef __NVCC__

#include "sunshine/platform/common.h"
#include "x11grab.h"

namespace cuda {
std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, platf::x11::xdisplay_t::pointer xdisplay);
int init();
} // namespace cuda

#else
namespace platf {
class img_t;
}
#endif

typedef struct cudaArray *cudaArray_t;

#if !defined(__CUDACC__)
typedef unsigned long long cudaTextureObject_t;
#else  /* defined(__CUDACC__) */
typedef __location__(device_builtin) unsigned long long cudaTextureObject_t;
#endif /* !defined(__CUDACC__) */

namespace cuda {

class freeCudaPtr_t {
public:
  void operator()(void *ptr);
};

using ptr_t = std::unique_ptr<void, freeCudaPtr_t>;

struct viewport_t {
  int width, height;
  int offsetX, offsetY;
};

class sws_t {
public:
  ~sws_t();
  sws_t(int in_width, int in_height, int out_width, int out_height, int pitch, int threadsPerBlock, ptr_t &&color_matrix);

  /**
   * in_width, in_height -- The width and height of the captured image in pixels
   * out_width, out_height -- the width and height of the NV12 image in pixels
   * 
   * pitch -- The size of a single row of pixels in bytes
   */
  static std::unique_ptr<sws_t> make(int in_width, int in_height, int out_width, int out_height, int pitch);

  // Converts loaded image into a CUDevicePtr
  int convert(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV);
  int convert(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV, const viewport_t &viewport);

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range);

  int load_ram(platf::img_t &img);

  ptr_t color_matrix;
  cudaArray_t array;
  cudaTextureObject_t texture;

  int threadsPerBlock;

  viewport_t viewport;
};
} // namespace cuda

#endif