#ifndef SUNSHINE_PLATFORM_CUDA_H
#define SUNSHINE_PLATFORM_CUDA_H

#include "sunshine/platform/common.h"
#include "x11grab.h"

namespace cuda {
std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, platf::x11::xdisplay_t::pointer xdisplay);
int init();
} // namespace cuda

#endif