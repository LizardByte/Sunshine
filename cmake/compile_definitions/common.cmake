# common compile definitions
# this file will also load platform specific definitions

list(APPEND SUNSHINE_COMPILE_OPTIONS -Wall -Wno-sign-compare)
# Wall - enable all warnings
# Wno-sign-compare - disable warnings for signed/unsigned comparisons

# setup assets directory
if(NOT SUNSHINE_ASSETS_DIR)
    set(SUNSHINE_ASSETS_DIR "assets")
endif()

# platform specific compile definitions
if(WIN32)
    include(${CMAKE_MODULE_PATH}/compile_definitions/windows.cmake)
elseif(UNIX)
    include(${CMAKE_MODULE_PATH}/compile_definitions/unix.cmake)

    if(APPLE)
        include(${CMAKE_MODULE_PATH}/compile_definitions/macos.cmake)
    else()
        include(${CMAKE_MODULE_PATH}/compile_definitions/linux.cmake)
    endif()
endif()

include_directories(SYSTEM third-party/nv-codec-headers/include)
file(GLOB NVENC_SOURCES CONFIGURE_DEPENDS "src/nvenc/*.cpp" "src/nvenc/*.h")
list(APPEND PLATFORM_TARGET_FILES ${NVENC_SOURCES})

configure_file(src/version.h.in version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(SUNSHINE_TARGET_FILES
        third-party/nanors/rs.c
        third-party/nanors/rs.h
        third-party/moonlight-common-c/src/Input.h
        third-party/moonlight-common-c/src/Rtsp.h
        third-party/moonlight-common-c/src/RtspParser.c
        third-party/moonlight-common-c/src/Video.h
        third-party/tray/tray.h
        src/upnp.cpp
        src/upnp.h
        src/cbs.cpp
        src/utility.h
        src/uuid.h
        src/config.h
        src/config.cpp
        src/main.cpp
        src/main.h
        src/crypto.cpp
        src/crypto.h
        src/nvhttp.cpp
        src/nvhttp.h
        src/httpcommon.cpp
        src/httpcommon.h
        src/confighttp.cpp
        src/confighttp.h
        src/rtsp.cpp
        src/rtsp.h
        src/stream.cpp
        src/stream.h
        src/video.cpp
        src/video.h
        src/video_colorspace.cpp
        src/video_colorspace.h
        src/input.cpp
        src/input.h
        src/audio.cpp
        src/audio.h
        src/platform/common.h
        src/process.cpp
        src/process.h
        src/network.cpp
        src/network.h
        src/move_by_copy.h
        src/system_tray.cpp
        src/system_tray.h
        src/task_pool.h
        src/thread_pool.h
        src/thread_safe.h
        src/sync.h
        src/round_robin.h
        src/stat_trackers.h
        src/stat_trackers.cpp
        ${PLATFORM_TARGET_FILES})

set_source_files_properties(src/upnp.cpp PROPERTIES COMPILE_FLAGS -Wno-pedantic)

set_source_files_properties(third-party/nanors/rs.c
        PROPERTIES COMPILE_FLAGS "-include deps/obl/autoshim.h -ftree-vectorize")

if(NOT SUNSHINE_ASSETS_DIR_DEF)
    set(SUNSHINE_ASSETS_DIR_DEF "${SUNSHINE_ASSETS_DIR}")
endif()
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR_DEF}")

list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_TRAY=${SUNSHINE_TRAY})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
        SYSTEM
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/moonlight-common-c/enet/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/nanors
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/nanors/deps/obl
        ${FFMPEG_INCLUDE_DIRS}
        ${PLATFORM_INCLUDE_DIRS}
)

string(TOUPPER "x${CMAKE_BUILD_TYPE}" BUILD_TYPE)
if("${BUILD_TYPE}" STREQUAL "XDEBUG")
    if(WIN32)
        set_source_files_properties(src/nvhttp.cpp PROPERTIES COMPILE_FLAGS -O2)
    endif()
else()
    add_definitions(-DNDEBUG)
endif()

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${MINIUPNP_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        enet
        opus
        ${FFMPEG_LIBRARIES}
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${CURL_LIBRARIES}
        ${PLATFORM_LIBRARIES})
