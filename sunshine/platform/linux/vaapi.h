#ifndef SUNSHINE_VAAPI_H
#define SUNSHINE_VAAPI_H

#include "misc.h"
#include "sunshine/platform/common.h"

namespace egl {
struct surface_descriptor_t;
}
namespace va {
std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height);
std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, file_t &&card, int offset_x, int offset_y, const egl::surface_descriptor_t &sd);

// Ensure the render device pointed to by fd is capable of encoding h264
bool validate(int fd);

int init();
} // namespace va
#endif