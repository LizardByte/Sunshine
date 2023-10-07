# macos specific compile definitions

add_compile_definitions(SUNSHINE_PLATFORM="macos")

link_directories(/opt/local/lib)
link_directories(/usr/local/lib)
link_directories(/opt/homebrew/lib)
ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${APP_SERVICES_LIBRARY}
        ${AV_FOUNDATION_LIBRARY}
        ${CORE_MEDIA_LIBRARY}
        ${CORE_VIDEO_LIBRARY}
        ${VIDEO_TOOLBOX_LIBRARY}
        ${FOUNDATION_LIBRARY})

set(PLATFORM_INCLUDE_DIRS
        ${Boost_INCLUDE_DIR})

set(APPLE_PLIST_FILE ${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/Info.plist)

# todo - tray is not working on macos
set(SUNSHINE_TRAY 0)

set(PLATFORM_TARGET_FILES
        src/platform/macos/av_audio.h
        src/platform/macos/av_audio.m
        src/platform/macos/av_img_t.h
        src/platform/macos/av_video.h
        src/platform/macos/av_video.m
        src/platform/macos/display.mm
        src/platform/macos/input.cpp
        src/platform/macos/microphone.mm
        src/platform/macos/misc.mm
        src/platform/macos/misc.h
        src/platform/macos/nv12_zero_device.cpp
        src/platform/macos/nv12_zero_device.h
        src/platform/macos/publish.cpp
        third-party/TPCircularBuffer/TPCircularBuffer.c
        third-party/TPCircularBuffer/TPCircularBuffer.h
        ${APPLE_PLIST_FILE})

if(SUNSHINE_ENABLE_TRAY)
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
            ${COCOA})
    list(APPEND PLATFORM_TARGET_FILES
            third-party/tray/tray_darwin.m)
endif()
