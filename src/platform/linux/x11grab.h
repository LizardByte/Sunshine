#ifndef SUNSHINE_X11_GRAB
#define SUNSHINE_X11_GRAB

#include <optional>

#include "src/platform/common.h"
#include "src/utility.h"

// X11 Display
extern "C" struct _XDisplay;

namespace egl {
class cursor_t;
}

namespace platf::x11 {

#ifdef SUNSHINE_BUILD_X11
struct cursor_ctx_raw_t;
void freeCursorCtx(cursor_ctx_raw_t *ctx);
void freeDisplay(_XDisplay *xdisplay);

using cursor_ctx_t = util::safe_ptr<cursor_ctx_raw_t, freeCursorCtx>;
using xdisplay_t   = util::safe_ptr<_XDisplay, freeDisplay>;

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

xdisplay_t make_display();
#else
// It's never something different from nullptr
util::safe_ptr<_XDisplay, std::default_delete<_XDisplay>>;

class cursor_t {
public:
  static std::optional<cursor_t> make() { return std::nullopt; }

  void capture(egl::cursor_t &) {}
  void blend(img_t &, int, int) {}
};

xdisplay_t make_display() { return nullptr; }
#endif
} // namespace platf::x11

#endif