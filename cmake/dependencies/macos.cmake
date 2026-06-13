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
# IOKit is needed for IOHIDUserDevice* (virtual gamepad device — hid_gamepad.m).
# Actually creating devices at runtime requires the user to disable AMFI via
# `nvram boot-args="amfi_get_out_of_my_way=1"`, but the symbols themselves
# are unconditionally present and the host alloc_gamepad path probes
# availability before relying on them.
FIND_LIBRARY(IO_KIT_LIBRARY IOKit)
FIND_LIBRARY(VIDEO_TOOLBOX_LIBRARY VideoToolbox)

if(SUNSHINE_ENABLE_TRAY)
    FIND_LIBRARY(COCOA Cocoa REQUIRED)
endif()
