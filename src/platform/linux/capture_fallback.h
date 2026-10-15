/**
 * @file src/platform/linux/capture_fallback.h
 * @brief Select a deferred KWin capture source without starting competing Portal repair.
 */
#pragma once

#include <string_view>

namespace platf::capture {
  /** @brief Additional source selected after the existing output-based probes. */
  enum class fallback_e {
    none,  ///< Keep the previous selection, or no usable source exists.
    kwin,  ///< KWin supports ScreenCast; outputs may be created later.
    portal  ///< Portal already has a usable capture session.
  };

  /**
   * @brief Finish source selection while keeping an already usable Portal ahead of headless KWin.
   * @tparam KwinProbe Callable that reports KWin ScreenCast capability.
   * @tparam PortalProbe Callable that verifies Portal with optional permission repair.
   * @param requested Original capture option, before explicit Portal fallback handling.
   * @param source_selected Whether an earlier probe already selected a source.
   * @param probe_kwin Probe ScreenCast capability without starting a stream; false when not built.
   * @param probe_portal Verify Portal, accepting a flag that permits permission repair/restart;
   *                     false when not built. Repair is suppressed when KWin is usable.
   * @return Additional source to select. Explicit unrelated capture choices are unchanged.
   */
  template<class KwinProbe, class PortalProbe>
  fallback_e probe_fallback(std::string_view requested, bool source_selected, KwinProbe probe_kwin, PortalProbe probe_portal) {
    using enum fallback_e;
    if (source_selected || (!requested.empty() && requested != "kwin")) {
      return none;
    }
    const bool kwin_available = probe_kwin();
    if (requested.empty() && probe_portal(!kwin_available)) {
      return portal;
    }
    return kwin_available ? kwin : none;
  }
}  // namespace platf::capture
