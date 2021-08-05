#ifndef SUNSHINE_DISPLAY_H
#define SUNSHINE_DISPLAY_H

#include "sunshine/platform/common.h"
namespace va {
std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height);
} // namespace va
#endif