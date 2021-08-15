#ifndef SUNSHINE_X11_GRAB
#define SUNSHINE_X11_GRAB

#include <optional>

#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

namespace egl {
class cursor_t;
}

namespace platf::x11 {

#ifdef SUNSHINE_BUILD_X11
struct cursor_ctx_raw_t;
void freeCursorCtx(cursor_ctx_raw_t *ctx);

using cursor_ctx_t = util::safe_ptr<cursor_ctx_raw_t, freeCursorCtx>;

class cursor_t {
public:
  static std::optional<cursor_t> make();

  void capture(egl::cursor_t &img);

  /**
   * Capture and blend the cursor into the image
   * 
   * img <-- destination image
   * offsetX, offsetY <--- Top left corner of the virtual screen
   */
  void blend(img_t &img, int offsetX, int offsetY);

  cursor_ctx_t ctx;
};
#else
class cursor_t {
public:
  static std::optional<cursor_t> make() { return std::nullopt; }

  void capture(egl::cursor_t &) {}
  void blend(img_t &, int, int) {}
};
#endif
} // namespace platf::x11

#endif