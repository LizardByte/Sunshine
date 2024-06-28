/**
 * @file src/platform/linux/x11grab.h
 * @brief Declarations for x11 capture.
 */
#pragma once

#include <optional>

#include "src/platform/common.h"
#include "src/utility.h"

// X11 Display
extern "C" struct _XDisplay;

namespace egl {
  class cursor_t;
}

namespace platf::x11 {
  struct cursor_ctx_raw_t;
  void
  freeCursorCtx(cursor_ctx_raw_t *ctx);
  void
  freeDisplay(_XDisplay *xdisplay);

  using cursor_ctx_t = util::safe_ptr<cursor_ctx_raw_t, freeCursorCtx>;
  using xdisplay_t = util::safe_ptr<_XDisplay, freeDisplay>;

  class cursor_t {
  public:
    static std::optional<cursor_t>
    make();

    void
    capture(egl::cursor_t &img);

    /**
     * Capture and blend the cursor into the image
     *
     * img <-- destination image
     * offsetX, offsetY <--- Top left corner of the virtual screen
     */
    void
    blend(img_t &img, int offsetX, int offsetY);

    cursor_ctx_t ctx;
  };

  xdisplay_t
  make_display();
}  // namespace platf::x11
