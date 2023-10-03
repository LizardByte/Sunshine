# macos specific dependencies

FIND_LIBRARY(APP_SERVICES_LIBRARY ApplicationServices)
FIND_LIBRARY(AV_FOUNDATION_LIBRARY AVFoundation)
FIND_LIBRARY(CORE_MEDIA_LIBRARY CoreMedia)
FIND_LIBRARY(CORE_VIDEO_LIBRARY CoreVideo)
FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(VIDEO_TOOLBOX_LIBRARY VideoToolbox)

if(SUNSHINE_ENABLE_TRAY)
    FIND_LIBRARY(COCOA Cocoa REQUIRED)
endif()

set(Boost_USE_STATIC_LIBS ON)  # cmake-lint: disable=C0103
# workaround to prevent link errors against icudata, icui18n
set(Boost_NO_BOOST_CMAKE ON)  # cmake-lint: disable=C0103
