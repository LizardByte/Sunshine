# common compile definitions
# this file will also load platform specific definitions

list(APPEND SUNSHINE_COMPILE_OPTIONS -Wall -Wno-sign-compare)
# Wall - enable all warnings
# Werror - treat warnings as errors
# Wno-maybe-uninitialized/Wno-uninitialized - disable warnings for maybe uninitialized variables
# Wno-sign-compare - disable warnings for signed/unsigned comparisons
# Wno-restrict - disable warnings for memory overlap

# ----------------------------------------------------------------------------
# CachyOS / Linux local-LAN fast-path build flags.
#
# When SUNSHINE_CACHYOS_NATIVE is ON (the default on Linux), we:
#   - march on Zen 2 (znver2) so AVX2/BMI2/FMA paths get picked up
#   - march on Zen 3 (znver3) or Zen 4 (znver4) when the host supports it
#   - enable -O3 -flto so the encoder/color-conversion hot loops get
#     vectorised and dead-store eliminated across translation units
#   - drop frame pointers and PLT indirections (small but free win)
#
# Disable by passing -DSUNSHINE_CACHYOS_NATIVE=OFF if you ship a generic
# Linux binary or run on a different microarch.
# ----------------------------------------------------------------------------
if(UNIX AND NOT APPLE AND NOT DEFINED SUNSHINE_CACHYOS_NATIVE)
    set(SUNSHINE_CACHYOS_NATIVE ON CACHE BOOL
        "Optimise for the build host's microarchitecture (CachyOS-style).")
endif()

if(SUNSHINE_CACHYOS_NATIVE AND UNIX AND NOT APPLE)
    set(_sunshine_native_march "")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(amd64|AMD64|x86_64|X86_64)")
        # Try to detect a known Zen generation via /proc/cpuinfo on the build
        # host. Falls back to x86-64-v3 (AVX2 baseline) which CachyOS ships.
        set(_sunshine_cpuinfo_file "/proc/cpuinfo")
        if(EXISTS ${_sunshine_cpuinfo_file})
            file(READ ${_sunshine_cpuinfo_file} _sunshine_cpuinfo)
            string(TOLOWER "${_sunshine_cpuinfo}" _sunshine_cpuinfo_lower)
            if(_sunshine_cpuinfo_lower MATCHES "model name.*zen 4")
                set(_sunshine_native_march "znver4")
            elseif(_sunshine_cpuinfo_lower MATCHES "model name.*zen 3")
                set(_sunshine_native_march "znver3")
            elseif(_sunshine_cpuinfo_lower MATCHES "model name.*zen 2")
                set(_sunshine_native_march "znver2")
            elseif(_sunshine_cpuinfo_lower MATCHES "model name.*zen")
                set(_sunshine_native_march "znver1")
            endif()
        endif()
        if(NOT _sunshine_native_march)
            # CachyOS kernels/tools and Steam Deck's steamrt are typically
            # built with x86-64-v3. v3 = AVX2 + BMI2 + FMA, safe on every
            # CachyOS-supported CPU since the original Ryzen launch.
            # We only use it for -march; GCC's -mtune doesn't understand
            # microarch levels, so we fall back to -mtune=generic.
            set(_sunshine_native_march "x86-64-v3")
            set(_sunshine_native_mtune "generic")
        endif()
        # znver2/znver3/znver4 are valid for both -march and -mtune on GCC.
        # x86-64-v3 is only valid for -march; -mtune has to be generic.
        if("${_sunshine_native_march}" STREQUAL "x86-64-v3")
            set(_sunshine_native_mtune "generic")
        else()
            set(_sunshine_native_mtune "${_sunshine_native_march}")
        endif()
        message(STATUS "CachyOS native build: -march=${_sunshine_native_march} -mtune=${_sunshine_native_mtune}")
        list(APPEND SUNSHINE_COMPILE_OPTIONS
            "-march=${_sunshine_native_march}"
            "-mtune=${_sunshine_native_mtune}"
            "-O3"
            "-fno-plt"
            "-fomit-frame-pointer")
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            # LTO only at Release; Debug builds stay debuggable.
            list(APPEND SUNSHINE_COMPILE_OPTIONS "-flto=auto")
        endif()
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC specific compile options

    # GCC 12 and higher will complain about maybe-uninitialized
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12)
        list(APPEND SUNSHINE_COMPILE_OPTIONS -Wno-maybe-uninitialized)

        # Disable the bogus warning that may prevent compilation (only for GCC 12).
        # See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105651.
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
            list(APPEND SUNSHINE_COMPILE_OPTIONS -Wno-restrict)
        endif()
    endif()

    # GCC 15 will complain about uninitialized variables in some cases (Simple-Web-Server)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
        list(APPEND SUNSHINE_COMPILE_OPTIONS -Wno-uninitialized)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Clang specific compile options

    # Clang doesn't actually complain about this this, so disabling for now
    # list(APPEND SUNSHINE_COMPILE_OPTIONS -Wno-uninitialized)
endif()
if(BUILD_WERROR)
    list(APPEND SUNSHINE_COMPILE_OPTIONS -Werror)
endif()

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

include_directories(BEFORE SYSTEM "${CMAKE_SOURCE_DIR}/third-party/nv-codec-headers/include")
file(GLOB NVENC_SOURCES CONFIGURE_DEPENDS "src/nvenc/*.cpp" "src/nvenc/*.h")
list(APPEND PLATFORM_TARGET_FILES ${NVENC_SOURCES})

set(SUNSHINE_TARGET_FILES
        "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/src/Input.h"
        "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/src/Rtsp.h"
        "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/src/RtspParser.c"
        "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/src/Video.h"
        "${CMAKE_SOURCE_DIR}/src/upnp.cpp"
        "${CMAKE_SOURCE_DIR}/src/upnp.h"
        "${CMAKE_SOURCE_DIR}/src/cbs.cpp"
        "${CMAKE_SOURCE_DIR}/src/utility.h"
        "${CMAKE_SOURCE_DIR}/src/uuid.h"
        "${CMAKE_SOURCE_DIR}/src/config.h"
        "${CMAKE_SOURCE_DIR}/src/config.cpp"
        "${CMAKE_SOURCE_DIR}/src/display_device.h"
        "${CMAKE_SOURCE_DIR}/src/display_device.cpp"
        "${CMAKE_SOURCE_DIR}/src/entry_handler.cpp"
        "${CMAKE_SOURCE_DIR}/src/entry_handler.h"
        "${CMAKE_SOURCE_DIR}/src/file_handler.cpp"
        "${CMAKE_SOURCE_DIR}/src/file_handler.h"
        "${CMAKE_SOURCE_DIR}/src/globals.cpp"
        "${CMAKE_SOURCE_DIR}/src/globals.h"
        "${CMAKE_SOURCE_DIR}/src/logging.cpp"
        "${CMAKE_SOURCE_DIR}/src/logging.h"
        "${CMAKE_SOURCE_DIR}/src/main.cpp"
        "${CMAKE_SOURCE_DIR}/src/main.h"
        "${CMAKE_SOURCE_DIR}/src/crypto.cpp"
        "${CMAKE_SOURCE_DIR}/src/crypto.h"
        "${CMAKE_SOURCE_DIR}/src/nvhttp.cpp"
        "${CMAKE_SOURCE_DIR}/src/nvhttp.h"
        "${CMAKE_SOURCE_DIR}/src/httpcommon.cpp"
        "${CMAKE_SOURCE_DIR}/src/httpcommon.h"
        "${CMAKE_SOURCE_DIR}/src/confighttp.cpp"
        "${CMAKE_SOURCE_DIR}/src/confighttp.h"
        "${CMAKE_SOURCE_DIR}/src/rtsp.cpp"
        "${CMAKE_SOURCE_DIR}/src/rtsp.h"
        "${CMAKE_SOURCE_DIR}/src/stream.cpp"
        "${CMAKE_SOURCE_DIR}/src/stream.h"
        "${CMAKE_SOURCE_DIR}/src/video.cpp"
        "${CMAKE_SOURCE_DIR}/src/video.h"
        "${CMAKE_SOURCE_DIR}/src/video_colorspace.cpp"
        "${CMAKE_SOURCE_DIR}/src/video_colorspace.h"
        "${CMAKE_SOURCE_DIR}/src/input.cpp"
        "${CMAKE_SOURCE_DIR}/src/input.h"
        "${CMAKE_SOURCE_DIR}/src/audio.cpp"
        "${CMAKE_SOURCE_DIR}/src/audio.h"
        "${CMAKE_SOURCE_DIR}/src/platform/common.h"
        "${CMAKE_SOURCE_DIR}/src/process.cpp"
        "${CMAKE_SOURCE_DIR}/src/process.h"
        "${CMAKE_SOURCE_DIR}/src/network.cpp"
        "${CMAKE_SOURCE_DIR}/src/network.h"
        "${CMAKE_SOURCE_DIR}/src/move_by_copy.h"
        "${CMAKE_SOURCE_DIR}/src/system_tray.cpp"
        "${CMAKE_SOURCE_DIR}/src/system_tray.h"
        "${CMAKE_SOURCE_DIR}/src/task_pool.h"
        "${CMAKE_SOURCE_DIR}/src/thread_pool.h"
        "${CMAKE_SOURCE_DIR}/src/thread_safe.h"
        "${CMAKE_SOURCE_DIR}/src/sync.h"
        "${CMAKE_SOURCE_DIR}/src/round_robin.h"
        "${CMAKE_SOURCE_DIR}/src/stat_trackers.h"
        "${CMAKE_SOURCE_DIR}/src/stat_trackers.cpp"
        "${CMAKE_SOURCE_DIR}/src/rswrapper.h"
        "${CMAKE_SOURCE_DIR}/src/rswrapper.c"
        ${PLATFORM_TARGET_FILES})

if(NOT SUNSHINE_ASSETS_DIR_DEF)
    set(SUNSHINE_ASSETS_DIR_DEF "${SUNSHINE_ASSETS_DIR}")
endif()
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR_DEF}")

# Publisher metadata
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_PUBLISHER_NAME="${SUNSHINE_PUBLISHER_NAME}")
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_PUBLISHER_WEBSITE="${SUNSHINE_PUBLISHER_WEBSITE}")
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_PUBLISHER_ISSUE_URL="${SUNSHINE_PUBLISHER_ISSUE_URL}")

include_directories(BEFORE "${CMAKE_SOURCE_DIR}")

include_directories(
        BEFORE
        SYSTEM
        "${CMAKE_SOURCE_DIR}/third-party"
        "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/enet/include"
        "${CMAKE_SOURCE_DIR}/third-party/nanors"
        "${CMAKE_SOURCE_DIR}/third-party/nanors/deps/obl"
        ${OPENSSL_INCLUDE_DIR}
        ${Opus_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}  # has to be the last, or we get runtime error on macOS ffmpeg encoder
)

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${MINIUPNP_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        enet
        libdisplaydevice::display_device
        nlohmann_json::nlohmann_json
        ${Opus_LIBRARY}
        ${FFMPEG_LIBRARIES}
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${PLATFORM_LIBRARIES})

# tray icon
if(SUNSHINE_ENABLE_TRAY)
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES tray::tray)
else()
    set(SUNSHINE_TRAY 0)
    message(STATUS "Tray icon disabled")
endif()
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_TRAY=${SUNSHINE_TRAY})
