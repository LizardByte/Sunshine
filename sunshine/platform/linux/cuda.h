#ifndef SUNSHINE_PLATFORM_CUDA_H
#define SUNSHINE_PLATFORM_CUDA_H

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
class sws_t {
public:
  ~sws_t();
  sws_t(int in_width, int in_height, int out_width, int out_height, int threadsPerBlock);

  /**
   * in_width, out_width -- The width and height of the captured image in bytes
   * out_width, out_height -- the width and height of the NV12 image in pixels
   * 
   * cuda_device -- pointer to the cuda device
   */
  static std::unique_ptr<sws_t> make(int in_width, int in_height, int out_width, int out_height);

  // Converts loaded image into a CUDevicePtr
  int convert(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV);

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range);

  int load_ram(platf::img_t &img);

  cudaArray_t array;
  cudaTextureObject_t texture;

  int width, height;

  int threadsPerBlock;
};
} // namespace cuda

#endif