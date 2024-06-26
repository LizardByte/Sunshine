/**
 * @file src/platform/macos/misc.h
 * @brief Miscellaneous declarations for macOS platform.
 */
#pragma once

#include <vector>

#include <CoreGraphics/CoreGraphics.h>

namespace dyn {
  typedef void (*apiproc)();

  int
  load(void *handle, const std::vector<std::tuple<apiproc *, const char *>> &funcs, bool strict = true);
  void *
  handle(const std::vector<const char *> &libs);

}  // namespace dyn
