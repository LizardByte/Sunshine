# macos specific dependencies

FIND_LIBRARY(APP_KIT_LIBRARY AppKit)
FIND_LIBRARY(APP_SERVICES_LIBRARY ApplicationServices)
FIND_LIBRARY(AUDIO_TOOLBOX_LIBRARY AudioToolbox)
FIND_LIBRARY(AUDIO_UNIT_LIBRARY AudioUnit)
FIND_LIBRARY(AV_FOUNDATION_LIBRARY AVFoundation)
FIND_LIBRARY(CORE_AUDIO_LIBRARY CoreAudio)
FIND_LIBRARY(CORE_MEDIA_LIBRARY CoreMedia)
FIND_LIBRARY(CORE_VIDEO_LIBRARY CoreVideo)
FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(VIDEO_TOOLBOX_LIBRARY VideoToolbox)
# ScreenCaptureKit is the modern (macOS 12.3+) replacement for the
# deprecated AVCaptureScreenInput-based capture path. Sunshine's
# sc_video.{h,m} is unconditionally compiled into the macOS target;
# fail configure with a clear message rather than failing the build
# later on header lookup when the SDK doesn't ship the framework
# (e.g., when building with an Xcode older than 13.3 / SDK older than
# 12.3, which dropped out of routine compatibility long ago).
FIND_LIBRARY(SCREEN_CAPTURE_KIT_LIBRARY ScreenCaptureKit REQUIRED)

if(SUNSHINE_ENABLE_TRAY)
    FIND_LIBRARY(COCOA Cocoa REQUIRED)
endif()
