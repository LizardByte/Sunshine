/**
 * @file src/platform/macos/misc.h
 * @brief Miscellaneous declarations for macOS platform.
 */
#pragma once

// standard includes
#include <array>
#include <optional>
#include <vector>

// platform includes
#include <CoreGraphics/CoreGraphics.h>

namespace platf {
  /**
   * @brief Check whether macOS has granted screen-capture permission.
   *
   * @return True when Sunshine can capture the screen.
   */
  bool is_screen_capture_allowed();

  /**
   * @brief Where the focused application is expecting text, as a fraction of the streamed display.
   *
   * A client whose on-screen keyboard covers half the picture has no way of knowing which half
   * matters. The host does: the focused element knows where its insertion point is, and
   * Accessibility will say so. Normalised to 0..1 of the display so the client needs to know
   * nothing about resolutions.
   *
   * Empty when the focused application does not report an insertion point — which is most of
   * them — or when the caret is on a display other than the one being streamed.
   *
   * @return {x, y, width, height} in 0..1 of the streamed display, or nothing.
   */
  std::optional<std::array<double, 4>> focused_caret();
}  // namespace platf

namespace dyn {
  typedef void (*apiproc)();

  /**
   * @brief Load persisted state from its backing store.
   *
   * @param handle Native library or object handle used by the operation.
   * @param funcs Function table populated from the loaded library.
   * @param strict Whether missing functions should be treated as an error.
   * @return 0 when all required symbols are loaded; nonzero when loading fails.
   */
  int load(void *handle, const std::vector<std::tuple<apiproc *, const char *>> &funcs, bool strict = true);
  /**
   * @brief Return the native handle owned by the wrapper.
   *
   * @param libs List of libraries to probe for the requested symbol.
   * @return Native dynamic-library handle, or nullptr when no library can be opened.
   */
  void *handle(const std::vector<const char *> &libs);

}  // namespace dyn
