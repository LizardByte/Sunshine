#ifndef SUNSHINE_DISPLAY_H
#define SUNSHINE_DISPLAY_H

#include "sunshine/platform/common.h"
namespace platf::egl {
std::shared_ptr<hwdevice_t> make_hwdevice(int width, int height);
} // namespace platf::egl
#endif