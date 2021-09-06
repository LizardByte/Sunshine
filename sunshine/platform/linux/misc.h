#ifndef SUNSHINE_PLATFORM_MISC_H
#define SUNSHINE_PLATFORM_MISC_H

#include <unistd.h>
#include <vector>

#include "sunshine/utility.h"

KITTY_USING_MOVE_T(file_t, int, -1, {
  if(el >= 0) {
    close(el);
  }
});

enum class window_system_e {
  NONE,
  X11,
  WAYLAND,
};

extern window_system_e window_system;

namespace dyn {
typedef void (*apiproc)(void);

int load(void *handle, const std::vector<std::tuple<apiproc *, const char *>> &funcs, bool strict = true);
void *handle(const std::vector<const char *> &libs);

} // namespace dyn

#endif