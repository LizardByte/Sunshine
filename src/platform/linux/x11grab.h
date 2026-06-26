/**
 * @file src/platform/linux/x11grab.h
 * @brief Declarations for x11 capture.
 */
#pragma once

// standard includes
#include <optional>

// local includes
#include "src/platform/common.h"
#include "src/utility.h"

// X11 Display
extern "C" struct _XDisplay;

namespace egl {
  class cursor_t;
}

namespace platf::x11 {
  struct cursor_ctx_raw_t;
  /**
   * @brief Release cursor context resources.
   *
   * @param ctx Native context object used by the operation or callback.
   */
  void freeCursorCtx(cursor_ctx_raw_t *ctx);
  /**
   * @brief Release display resources.
   *
   * @param xdisplay X11 display connection.
   */
  void freeDisplay(_XDisplay *xdisplay);

  /**
   * @brief XFixes cursor image pointer released with `XFree`.
   */
  using cursor_ctx_t = util::safe_ptr<cursor_ctx_raw_t, freeCursorCtx>;
  /**
   * @brief X11 display pointer released with `XCloseDisplay`.
   */
  using xdisplay_t = util::safe_ptr<_XDisplay, freeDisplay>;

  /**
   * @brief X11 cursor image and positioning state used during capture.
   */
  class cursor_t {
  public:
    /**
     * @brief Allocate the underlying object and wrap it in the owning handle.
     *
     * @return Created backend object, or null when creation fails.
     */
    static std::optional<cursor_t> make();

    /**
     * @brief Run the capture loop for this backend.
     *
     * @param img Image or frame object to read from or populate.
     */
    void capture(egl::cursor_t &img);

    /**
     * Capture and blend the cursor into the image
     *
     * img <-- destination image
     * offsetX, offsetY <--- Top left corner of the virtual screen
     * @param img Image or frame object to read from or populate.
     * @param offsetX Offset x.
     * @param offsetY Offset y.
     */
    void blend(img_t &img, int offsetX, int offsetY);

    cursor_ctx_t ctx;  ///< X11 cursor context used to track and blend cursor images.
  };

  /**
   * @brief Open and initialize the display connection used for capture.
   *
   * @return Constructed display object.
   */
  xdisplay_t make_display();
}  // namespace platf::x11
