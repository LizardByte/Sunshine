# macos specific compile definitions

add_compile_definitions(SUNSHINE_PLATFORM="macos")

if (SUNSHINE_BUILD_HOMEBREW)
    set(SUNSHINE_ASSETS_DIR "${CMAKE_INSTALL_PREFIX}/${SUNSHINE_ASSETS_DIR}")
else()
    # Bundle layout for macOS app builds
    set(SUNSHINE_ASSETS_DIR "${CMAKE_PROJECT_NAME}.app/Contents/Resources/assets")
    set(SUNSHINE_ASSETS_DIR_DEF "../Resources/assets")
endif()

set(MACOS_LINK_DIRECTORIES
        /opt/homebrew/lib
        /opt/local/lib
        /usr/local/lib)

foreach(dir ${MACOS_LINK_DIRECTORIES})
    if(EXISTS ${dir})
        link_directories(${dir})
    endif()
endforeach()

if(NOT BOOST_USE_STATIC AND NOT FETCH_CONTENT_BOOST_USED)
    ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)
endif()

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${APP_KIT_LIBRARY}
        ${APP_SERVICES_LIBRARY}
        ${AV_FOUNDATION_LIBRARY}
        ${CORE_MEDIA_LIBRARY}
        ${CORE_VIDEO_LIBRARY}
        ${FOUNDATION_LIBRARY}
        ${VIDEO_TOOLBOX_LIBRARY})

set(APPLE_PLIST_TEMPLATE "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/build/Info.plist.in")
set(APPLE_PLIST_FILE "${CMAKE_BINARY_DIR}/Info.plist")
configure_file("${APPLE_PLIST_TEMPLATE}" "${APPLE_PLIST_FILE}" @ONLY)

set(PLATFORM_TARGET_FILES
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_audio.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_audio.m"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_img_t.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_video.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_video.m"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/display.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/input.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/microphone.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/misc.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/misc.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/nv12_zero_device.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/nv12_zero_device.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/publish.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/TPCircularBuffer/TPCircularBuffer.c"
        "${CMAKE_SOURCE_DIR}/third-party/TPCircularBuffer/TPCircularBuffer.h"
        ${APPLE_PLIST_FILE})

if(SUNSHINE_ENABLE_TRAY)
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
            ${COCOA})
    list(APPEND PLATFORM_TARGET_FILES
            "${CMAKE_SOURCE_DIR}/third-party/tray/src/tray_darwin.m")
endif()
