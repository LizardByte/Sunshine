# macos specific dependencies

FIND_LIBRARY(APP_KIT_LIBRARY AppKit)
FIND_LIBRARY(APP_SERVICES_LIBRARY ApplicationServices)
FIND_LIBRARY(AV_FOUNDATION_LIBRARY AVFoundation)
FIND_LIBRARY(CORE_MEDIA_LIBRARY CoreMedia)
FIND_LIBRARY(CORE_VIDEO_LIBRARY CoreVideo)
FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(VIDEO_TOOLBOX_LIBRARY VideoToolbox)

if(SUNSHINE_ENABLE_TRAY)
    FIND_LIBRARY(COCOA Cocoa REQUIRED)
endif()

# Audio frameworks required for audio capture/processing
FIND_LIBRARY(AUDIO_TOOLBOX_LIBRARY AudioToolbox)
FIND_LIBRARY(AUDIO_UNIT_LIBRARY AudioUnit)
FIND_LIBRARY(CORE_AUDIO_LIBRARY CoreAudio)

include_directories(/opt/homebrew/opt/opus/include)
link_directories(/opt/homebrew/opt/opus/lib)