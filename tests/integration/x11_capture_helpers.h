/**
 * @file tests/integration/x11_capture_helpers.h
 * @brief X11 display helpers shared by the capture integration tests.
 */
#pragma once

#if defined(__linux__) && defined(SUNSHINE_BUILD_X11)

  // standard includes
  #include <memory>

  // platform includes
  #include <X11/Xlib.h>

  // local includes
  #include <src/platform/common.h>

namespace x11_test {
  /// Owning X display connection that is also released when an assertion aborts the test.
  using xdisplay_ptr = std::unique_ptr<Display, decltype(&XCloseDisplay)>;

  /**
   * @brief Draw a full-screen solid color on the X root window.
   *
   * @param xdisplay X display connection.
   * @param pixel Packed X pixel value to fill with.
   */
  inline void fill_root_window(Display *xdisplay, unsigned long pixel) {
    const auto screen = DefaultScreen(xdisplay);
    const auto root = RootWindow(xdisplay, screen);
    auto gc = XCreateGC(xdisplay, root, 0, nullptr);
    XSetForeground(xdisplay, gc, pixel);
    XFillRectangle(xdisplay, root, gc, 0, 0, DisplayWidth(xdisplay, screen), DisplayHeight(xdisplay, screen));
    XFreeGC(xdisplay, gc);
    XSync(xdisplay, False);
  }

  /**
   * @brief Capture a single frame through the display backend.
   *
   * @param disp Display backend to capture from.
   * @return Captured image, or null when capture failed.
   */
  inline std::shared_ptr<platf::img_t> capture_one_frame(platf::display_t &disp) {
    std::shared_ptr<platf::img_t> captured;
    auto push = [&captured](std::shared_ptr<platf::img_t> img, bool) {
      captured = std::move(img);
      return false;  // Stop after the first frame.
    };
    auto pull = [&disp](std::shared_ptr<platf::img_t> &img) {
      img = disp.alloc_img();
      return img != nullptr;
    };

    if (bool cursor = false; disp.capture(push, pull, &cursor) != platf::capture_e::ok) {
      return nullptr;
    }
    return captured;
  }
}  // namespace x11_test
#endif
