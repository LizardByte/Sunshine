/**
 * @file src/platform/macos/misc.h
 * @brief Miscellaneous declarations for macOS platform.
 */
#pragma once

#include <vector>

#include <CoreGraphics/CoreGraphics.h>

namespace platf {
  /**
   * Prompts the user for Accessibility permission
   * @return returns true if requested permission, false if already has permission
   */
  bool
  request_accessibility_permission();

  /**
   * Checks for Accessibility permission
   * @return returns true if sunshine has Accessibility permission enabled
   */
  bool
  has_accessibility_permission();
}

namespace dyn {
  typedef void (*apiproc)();

  int
  load(void *handle, const std::vector<std::tuple<apiproc *, const char *>> &funcs, bool strict = true);
  void *
  handle(const std::vector<const char *> &libs);

}  // namespace dyn
