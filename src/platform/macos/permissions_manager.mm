/**
 * @file src/platform/macos/permissions_manager.mm
 * @brief Handles macOS platform permissions.
 */

#include "permissions_manager.h"

#include <Foundation/Foundation.h>

#include "src/logging.h"

namespace platf {
// Even though the following two functions are available starting in macOS 10.15, they weren't
// actually in the Mac SDK until Xcode 12.2, the first to include the SDK for macOS 11
#if __MAC_OS_X_VERSION_MAX_ALLOWED < 110000  // __MAC_11_0
  // If they're not in the SDK then we can use our own function definitions.
  // Need to use weak import so that this will link in macOS 10.14 and earlier
  extern "C" bool
  CGPreflightScreenCaptureAccess(void) __attribute__((weak_import));
  extern "C" bool
  CGRequestScreenCaptureAccess(void) __attribute__((weak_import));
#endif

  namespace {
    auto
      screen_capture_allowed = std::atomic { false };
    /**
     * Used to avoid spamming permission requests when the user receives an input event
     */
    bool
      accessibility_permission_requested = std::atomic { false };
    bool
      has_accessibility = std::atomic { false };
  }  // namespace

  // Return whether screen capture is allowed for this process.
  bool
  PermissionsManager::is_screen_capture_allowed() {
    return screen_capture_allowed;
  }

  bool
  PermissionsManager::request_screen_capture_permission() {
    // This will generate a warning about CGPreflightScreenCaptureAccess and
    // CGRequestScreenCaptureAccess being unavailable before macOS 10.15, but
    // we have a guard to prevent it from being called on those earlier systems.
    // Unfortunately the supported way to silence this warning, using @available,
    // produces linker errors for __isPlatformVersionAtLeast, so we have to use
    // a different method.
    // We also ignore "tautological-pointer-compare" because when compiling with
    // Xcode 12.2 and later, these functions are not weakly linked and will never
    // be null, and therefore generate this warning. Since we are weakly linking
    // when compiling with earlier Xcode versions, the check for null is
    // necessary, and so we ignore the warning.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
#pragma clang diagnostic ignored "-Wtautological-pointer-compare"
    if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:((NSOperatingSystemVersion) { 10, 15, 0 })] &&
        // Double check that these weakly-linked symbols have been loaded:
        CGPreflightScreenCaptureAccess != nullptr && CGRequestScreenCaptureAccess != nullptr &&
        !CGPreflightScreenCaptureAccess()) {
      BOOST_LOG(error) << "No screen capture permission!";
      BOOST_LOG(error) << "Please activate it in 'System Preferences' -> 'Privacy' -> 'Screen Recording'";
      CGRequestScreenCaptureAccess();
      return true;
    }
#pragma clang diagnostic pop
    // Record that we determined that we have the screen capture permission.
    screen_capture_allowed = true;
    return false;
  }

  bool
  PermissionsManager::has_accessibility_permission() {
    NSDictionary *options = @{static_cast<id>(kAXTrustedCheckOptionPrompt): @NO};
    // We use kAXTrustedCheckOptionPrompt == NO here,
    // instead of using XIsProcessTrusted(),
    // because this will update the accessibility list with sunshine current path
    return AXIsProcessTrustedWithOptions(static_cast<CFDictionaryRef>(options));
  }

  bool
  PermissionsManager::has_accessibility_permission_cached() {
    if (has_accessibility) return true;
    if (accessibility_permission_requested) return has_accessibility;
    has_accessibility = has_accessibility_permission();
    return has_accessibility;
  }

  bool
  PermissionsManager::request_accessibility_permission() {
    if (!has_accessibility_permission()) {
      NSDictionary *options = @{static_cast<id>(kAXTrustedCheckOptionPrompt): @YES};
      return !AXIsProcessTrustedWithOptions(static_cast<CFDictionaryRef>(options));
    }
    return false;
  }

  bool
  PermissionsManager::request_accessibility_permission_once() {
    if (!accessibility_permission_requested) {
      accessibility_permission_requested = true;
      return request_accessibility_permission();
    }
    return false;
  }

  void
  PermissionsManager::print_accessibility_status(const bool is_keyboard_event, const bool release) {
    if (!release) return;

    if (!has_accessibility_permission_cached()) {
      request_accessibility_permission_once();
      BOOST_LOG(info) << "Received " << (is_keyboard_event ? "keyboard" : "mouse") << " event but "
                      << default_accessibility_log_msg();
    }
  }

}  // namespace platf
