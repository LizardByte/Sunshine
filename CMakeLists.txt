cmake_minimum_required(VERSION 3.18)
# `CMAKE_CUDA_ARCHITECTURES` requires 3.18

project(Sunshine VERSION 0.17.0
        DESCRIPTION "Sunshine is a self-hosted game stream host for Moonlight."
        HOMEPAGE_URL "https://app.lizardbyte.dev")

set(PROJECT_LONG_DESCRIPTION "Offering low latency, cloud gaming server capabilities with support for AMD, Intel, \
and Nvidia GPUs for hardware encoding. Software encoding is also available. You can connect to Sunshine from any \
Moonlight client on a variety of devices. A web UI is provided to allow configuration, and client pairing, from \
your favorite web browser. Pair from the local server or any mobile device.")

option(SUNSHINE_CONFIGURE_APPIMAGE "Configuration specific for AppImage." OFF)
option(SUNSHINE_CONFIGURE_AUR "Configure files required for AUR." OFF)
option(SUNSHINE_CONFIGURE_FLATPAK_MAN "Configure manifest file required for Flatpak build." OFF)
option(SUNSHINE_CONFIGURE_FLATPAK "Configuration specific for Flatpak." OFF)
option(SUNSHINE_CONFIGURE_PORTFILE "Configure macOS Portfile." OFF)
option(SUNSHINE_CONFIGURE_ONLY "Configure special files only, then exit." OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

if(${SUNSHINE_CONFIGURE_APPIMAGE})
    configure_file(packaging/linux/sunshine.desktop sunshine.desktop @ONLY)
elseif(${SUNSHINE_CONFIGURE_AUR})
    configure_file(packaging/linux/aur/PKGBUILD PKGBUILD @ONLY)
elseif(${SUNSHINE_CONFIGURE_FLATPAK_MAN})
    configure_file(packaging/linux/flatpak/dev.lizardbyte.sunshine.yml dev.lizardbyte.sunshine.yml @ONLY)
elseif(${SUNSHINE_CONFIGURE_PORTFILE})
    configure_file(packaging/macos/Portfile Portfile @ONLY)
endif()

# return if configure only is set
if(${SUNSHINE_CONFIGURE_ONLY})
    return()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(SUNSHINE_SOURCE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src_assets")

if(APPLE)
    # ADD_FRAMEWORK: args = `fwname`, `appname`
    macro(ADD_FRAMEWORK fwname appname)
        find_library(FRAMEWORK_${fwname}
                NAMES ${fwname}
                PATHS ${CMAKE_OSX_SYSROOT}/System/Library
                PATH_SUFFIXES Frameworks
                NO_DEFAULT_PATH)
        if( ${FRAMEWORK_${fwname}} STREQUAL FRAMEWORK_${fwname}-NOTFOUND)
            MESSAGE(ERROR ": Framework ${fwname} not found")
        else()
            TARGET_LINK_LIBRARIES(${appname} "${FRAMEWORK_${fwname}}/${fwname}")
            MESSAGE(STATUS "Framework ${fwname} found at ${FRAMEWORK_${fwname}}")
        endif()
    endmacro(ADD_FRAMEWORK)
endif()

add_subdirectory(third-party/moonlight-common-c/enet)
add_subdirectory(third-party/Simple-Web-Server)

set(UPNPC_BUILD_SHARED OFF CACHE BOOL "no shared libraries")
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Don't build tests for miniupnpc")
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Don't build samples for miniupnpc")
set(UPNPC_NO_INSTALL ON CACHE BOOL "Don't install any libraries build for miniupnpc")
add_subdirectory(third-party/miniupnp/miniupnpc)
include_directories(third-party/miniupnp/miniupnpc/include)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

if(NOT APPLE)
    set(Boost_USE_STATIC_LIBS ON)  # cmake-lint: disable=C0103
endif()
if(WIN32)
    # workaround to prevent link errors against icudata, icui18n
    set(Boost_NO_BOOST_CMAKE ON) # cmake-lint: disable=C0103
endif()

find_package(Boost COMPONENTS log filesystem program_options REQUIRED)

list(APPEND SUNSHINE_COMPILE_OPTIONS -Wall -Wno-missing-braces -Wno-maybe-uninitialized -Wno-sign-compare)

if(WIN32)
    enable_language(RC)
    set(CMAKE_RC_COMPILER windres)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CURL_STATIC_LDFLAGS} ${CURL_STATIC_CFLAGS}")

    add_compile_definitions(SUNSHINE_PLATFORM="windows")
    add_subdirectory(tools)  # This is temporary, only tools for Windows are needed, for now

    include_directories(third-party/ViGEmClient/include)

    if(NOT DEFINED SUNSHINE_ICON_PATH)
        set(SUNSHINE_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sunshine.ico")
    endif()
    configure_file(src/platform/windows/windows.rs.in windows.rc @ONLY)
    set(PLATFORM_TARGET_FILES
            "${CMAKE_CURRENT_BINARY_DIR}/windows.rc"
            src/platform/windows/publish.cpp
            src/platform/windows/misc.h
            src/platform/windows/misc.cpp
            src/platform/windows/input.cpp
            src/platform/windows/display.h
            src/platform/windows/display_base.cpp
            src/platform/windows/display_vram.cpp
            src/platform/windows/display_ram.cpp
            src/platform/windows/audio.cpp
            third-party/ViGEmClient/src/ViGEmClient.cpp
            third-party/ViGEmClient/include/ViGEm/Client.h
            third-party/ViGEmClient/include/ViGEm/Common.h
            third-party/ViGEmClient/include/ViGEm/Util.h
            third-party/ViGEmClient/include/ViGEm/km/BusShared.h)

    set(OPENSSL_LIBRARIES
            libssl.a
            libcrypto.a)

    list(PREPEND PLATFORM_LIBRARIES
            libstdc++.a
            libwinpthread.a
            libssp.a
            ksuser
            wsock32
            ws2_32
            d3d11 dxgi D3DCompiler
            setupapi
            dwmapi
            userenv
            synchronization.lib
            ${CURL_STATIC_LIBRARIES})

    set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp
            PROPERTIES COMPILE_DEFINITIONS "UNICODE=1;ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650")
    set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp
            PROPERTIES COMPILE_FLAGS "-Wno-unknown-pragmas -Wno-misleading-indentation -Wno-class-memaccess")
elseif(APPLE)
    add_compile_definitions(SUNSHINE_PLATFORM="macos")

    option(SUNSHINE_MACOS_PACKAGE "Should only be used when creating a MACOS package/dmg." OFF)

    link_directories(/opt/local/lib)
    link_directories(/usr/local/lib)
    ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)

    FIND_LIBRARY(APP_SERVICES_LIBRARY ApplicationServices )
    FIND_LIBRARY(AV_FOUNDATION_LIBRARY AVFoundation )
    FIND_LIBRARY(CORE_MEDIA_LIBRARY CoreMedia )
    FIND_LIBRARY(CORE_VIDEO_LIBRARY CoreVideo )
    FIND_LIBRARY(VIDEO_TOOLBOX_LIBRARY VideoToolbox )
    FIND_LIBRARY(FOUNDATION_LIBRARY Foundation )
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

    set(PLATFORM_TARGET_FILES
            src/platform/macos/av_audio.h
            src/platform/macos/av_audio.m
            src/platform/macos/av_img_t.h
            src/platform/macos/av_video.h
            src/platform/macos/av_video.m
            src/platform/macos/display.mm
            src/platform/macos/input.cpp
            src/platform/macos/microphone.mm
            src/platform/macos/misc.cpp
            src/platform/macos/misc.h
            src/platform/macos/nv12_zero_device.cpp
            src/platform/macos/nv12_zero_device.h
            src/platform/macos/publish.cpp
            third-party/TPCircularBuffer/TPCircularBuffer.c
            third-party/TPCircularBuffer/TPCircularBuffer.h
            ${APPLE_PLIST_FILE})
else()
    add_compile_definitions(SUNSHINE_PLATFORM="linux")

    option(SUNSHINE_ENABLE_DRM "Enable KMS grab if available" ON)
    option(SUNSHINE_ENABLE_X11 "Enable X11 grab if available" ON)
    option(SUNSHINE_ENABLE_WAYLAND "Enable building wayland specific code" ON)
    option(SUNSHINE_ENABLE_CUDA "Enable cuda specific code" ON)

    if(${SUNSHINE_ENABLE_X11})
        find_package(X11)
    else()
        set(X11_FOUND OFF)
    endif()

    set(CUDA_FOUND OFF)
    if(${SUNSHINE_ENABLE_CUDA})
        include(CheckLanguage)
        check_language(CUDA)

        if(CMAKE_CUDA_COMPILER)
            set(CUDA_FOUND ON)
            enable_language(CUDA)

            message(STATUS "CUDA Compiler Version: ${CMAKE_CUDA_COMPILER_VERSION}")
            set(CMAKE_CUDA_ARCHITECTURES "")

            # https://tech.amikelive.com/node-930/cuda-compatibility-of-nvidia-display-gpu-drivers/
            if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 6.5)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 10)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_10,code=sm_10")
            elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 6.5)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 50 52)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_50,code=sm_50")
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_52,code=sm_52")
            endif()

            if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 7.0)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 11)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_11,code=sm_11")
            elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER 7.6)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 60 61 62)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_60,code=sm_60")
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_61,code=sm_61")
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_62,code=sm_62")
            endif()

            if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 9.0)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 20)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_20,code=sm_20")
            elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 9.0)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 70)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_70,code=sm_70")
            endif()

            if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 10.0)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 75)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_75,code=sm_75")
            endif()

            if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 11.0)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 30)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_30,code=sm_30")
            elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 11.0)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 80)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_80,code=sm_80")
            endif()

            if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 11.1)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 86)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_86,code=sm_86")
            endif()

            if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 11.8)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 90)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_90,code=sm_90")
            endif()

            if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 12.0)
                list(APPEND CMAKE_CUDA_ARCHITECTURES 35)
                # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_35,code=sm_35")
            endif()

            # message(STATUS "CUDA NVCC Flags: ${CUDA_NVCC_FLAGS}")
            message(STATUS "CUDA Architectures: ${CMAKE_CUDA_ARCHITECTURES}")
        endif()
    endif()
    if(${SUNSHINE_ENABLE_DRM})
        find_package(LIBDRM)
        find_package(LIBCAP)
    else()
        set(LIBDRM_FOUND OFF)
        set(LIBCAP_FOUND OFF)
    endif()
    if(${SUNSHINE_ENABLE_WAYLAND})
        find_package(Wayland)
    else()
        set(WAYLAND_FOUND OFF)
    endif()

    if(X11_FOUND)
        add_compile_definitions(SUNSHINE_BUILD_X11)
        include_directories(${X11_INCLUDE_DIR})
        list(APPEND PLATFORM_TARGET_FILES src/platform/linux/x11grab.cpp)
    endif()

    if(CUDA_FOUND)
        include_directories(third-party/nvfbc)
        list(APPEND PLATFORM_TARGET_FILES
                src/platform/linux/cuda.cu
                src/platform/linux/cuda.cpp
                third-party/nvfbc/NvFBC.h)

        add_compile_definitions(SUNSHINE_BUILD_CUDA)
    endif()

    if(LIBDRM_FOUND AND LIBCAP_FOUND)
        add_compile_definitions(SUNSHINE_BUILD_DRM)
        include_directories(${LIBDRM_INCLUDE_DIRS} ${LIBCAP_INCLUDE_DIRS})
        list(APPEND PLATFORM_LIBRARIES ${LIBDRM_LIBRARIES} ${LIBCAP_LIBRARIES})
        list(APPEND PLATFORM_TARGET_FILES src/platform/linux/kmsgrab.cpp)
        list(APPEND SUNSHINE_DEFINITIONS EGL_NO_X11=1)
    elseif(LIBDRM_FOUND)
        message(WARNING "Found libdrm, yet there is no libcap")
    elseif(LIBDRM_FOUND)
        message(WARNING "Found libcap, yet there is no libdrm")
    endif()

    if(WAYLAND_FOUND)
        add_compile_definitions(SUNSHINE_BUILD_WAYLAND)
        # GEN_WAYLAND: args = `filename`
        macro(GEN_WAYLAND filename)
            file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated-src)

            message("wayland-scanner private-code \
${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${filename}.xml \
${CMAKE_BINARY_DIR}/generated-src/${filename}.c")
            message("wayland-scanner client-header \
${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${filename}.xml \
${CMAKE_BINARY_DIR}/generated-src/${filename}.h")
            execute_process(
                    COMMAND wayland-scanner private-code
                        ${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${filename}.xml
                        ${CMAKE_BINARY_DIR}/generated-src/${filename}.c
                    COMMAND wayland-scanner client-header
                        ${CMAKE_SOURCE_DIR}/third-party/wayland-protocols/${filename}.xml
                        ${CMAKE_BINARY_DIR}/generated-src/${filename}.h

                    RESULT_VARIABLE EXIT_INT
            )

            if(NOT ${EXIT_INT} EQUAL 0)
                message(FATAL_ERROR "wayland-scanner failed")
            endif()

            list(APPEND PLATFORM_TARGET_FILES
                    ${CMAKE_BINARY_DIR}/generated-src/${filename}.c
                    ${CMAKE_BINARY_DIR}/generated-src/${filename}.h)
        endmacro()

        GEN_WAYLAND(xdg-output-unstable-v1)
        GEN_WAYLAND(wlr-export-dmabuf-unstable-v1)

        include_directories(
                ${WAYLAND_INCLUDE_DIRS}
                ${CMAKE_BINARY_DIR}/generated-src
        )

        list(APPEND PLATFORM_LIBRARIES ${WAYLAND_LIBRARIES})
        list(APPEND PLATFORM_TARGET_FILES
                src/platform/linux/wlgrab.cpp
                src/platform/linux/wayland.cpp)
    endif()
    if(NOT ${X11_FOUND} AND NOT (${LIBDRM_FOUND} AND ${LIBCAP_FOUND}) AND NOT ${WAYLAND_FOUND} AND NOT ${})
        message(FATAL_ERROR "Couldn't find either x11, wayland, cuda or (libdrm and libcap)")
    endif()

    list(APPEND PLATFORM_TARGET_FILES
            src/platform/linux/publish.cpp
            src/platform/linux/vaapi.h
            src/platform/linux/vaapi.cpp
            src/platform/linux/cuda.h
            src/platform/linux/graphics.h
            src/platform/linux/graphics.cpp
            src/platform/linux/misc.h
            src/platform/linux/misc.cpp
            src/platform/linux/audio.cpp
            src/platform/linux/input.cpp
            src/platform/linux/x11grab.h
            src/platform/linux/wayland.h
            third-party/glad/src/egl.c
            third-party/glad/src/gl.c
            third-party/glad/include/EGL/eglplatform.h
            third-party/glad/include/KHR/khrplatform.h
            third-party/glad/include/glad/gl.h
            third-party/glad/include/glad/egl.h)

    list(APPEND PLATFORM_LIBRARIES
            dl
            evdev
            numa
            pulse
            pulse-simple)

    include_directories(
            /usr/include/libevdev-1.0
            third-party/nv-codec-headers/include
            third-party/glad/include)

    if(NOT DEFINED SUNSHINE_EXECUTABLE_PATH)
        set(SUNSHINE_EXECUTABLE_PATH "sunshine")
    endif()
    configure_file(sunshine.service.in sunshine.service @ONLY)
endif()

configure_file(version.h.in version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(SUNSHINE_TARGET_FILES
        third-party/nanors/rs.c
        third-party/nanors/rs.h
        third-party/moonlight-common-c/src/Input.h
        third-party/moonlight-common-c/src/Rtsp.h
        third-party/moonlight-common-c/src/RtspParser.c
        third-party/moonlight-common-c/src/Video.h
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
        src/task_pool.h
        src/thread_pool.h
        src/thread_safe.h
        src/sync.h
        src/round_robin.h
        ${PLATFORM_TARGET_FILES})

set_source_files_properties(src/upnp.cpp PROPERTIES COMPILE_FLAGS -Wno-pedantic)

set_source_files_properties(third-party/nanors/rs.c
        PROPERTIES COMPILE_FLAGS "-include deps/obl/autoshim.h -ftree-vectorize")

# Pre-compiled binaries
if(WIN32)
    set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffmpeg-windows-x86_64")
    set(FFMPEG_PLATFORM_LIBRARIES mfplat ole32 strmiids mfuuid mfx)
elseif(APPLE)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffmpeg-macos-aarch64")
    else()
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffmpeg-macos-x86_64")
    endif()
else()
    set(FFMPEG_PLATFORM_LIBRARIES va va-drm va-x11 vdpau X11)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffmpeg-linux-aarch64")
    else()
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffmpeg-linux-x86_64")
        list(APPEND FFMPEG_PLATFORM_LIBRARIES mfx)
    endif()
endif()
set(FFMPEG_INCLUDE_DIRS
        ${FFMPEG_PREPARED_BINARIES}/include)
if(EXISTS ${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a)
    set(HDR10_PLUS_LIBRARY
            ${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a)
endif()
set(FFMPEG_LIBRARIES
        ${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a
        ${FFMPEG_PREPARED_BINARIES}/lib/libavutil.a
        ${FFMPEG_PREPARED_BINARIES}/lib/libcbs.a
        ${FFMPEG_PREPARED_BINARIES}/lib/libSvtAv1Enc.a
        ${FFMPEG_PREPARED_BINARIES}/lib/libswscale.a
        ${FFMPEG_PREPARED_BINARIES}/lib/libx264.a
        ${FFMPEG_PREPARED_BINARIES}/lib/libx265.a
        ${HDR10_PLUS_LIBRARY}
        ${FFMPEG_PLATFORM_LIBRARIES})

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
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

# setup assets directory
if(NOT SUNSHINE_ASSETS_DIR)
    set(SUNSHINE_ASSETS_DIR "assets")
endif()
if(UNIX)
    set(SUNSHINE_ASSETS_DIR "${CMAKE_INSTALL_PREFIX}/${SUNSHINE_ASSETS_DIR}")
endif()

# use relative assets path for AppImage... maybe for all unix
if(${SUNSHINE_CONFIGURE_APPIMAGE})
    string(REPLACE "${CMAKE_INSTALL_PREFIX}" ".${CMAKE_INSTALL_PREFIX}" SUNSHINE_ASSETS_DIR_DEF ${SUNSHINE_ASSETS_DIR})
else()
    set(SUNSHINE_ASSETS_DIR_DEF "${SUNSHINE_ASSETS_DIR}")
endif()
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR_DEF}")

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        libminiupnpc-static
        ${CMAKE_THREAD_LIBS_INIT}
        enet
        opus
        ${FFMPEG_LIBRARIES}
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${CURL_LIBRARIES}
        ${PLATFORM_LIBRARIES})

if(NOT WIN32)
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES Boost::log)
endif()

add_executable(sunshine ${SUNSHINE_TARGET_FILES})

if(WIN32)
    set_target_properties(sunshine PROPERTIES LINK_SEARCH_START_STATIC 1)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
    find_library(ZLIB ZLIB1)
endif()

target_link_libraries(sunshine ${SUNSHINE_EXTERNAL_LIBRARIES} ${EXTRA_LIBS})
target_compile_definitions(sunshine PUBLIC ${SUNSHINE_DEFINITIONS})
set_target_properties(sunshine PROPERTIES CXX_STANDARD 17
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

if(NOT DEFINED CMAKE_CUDA_STANDARD)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

if(APPLE)
    target_link_options(sunshine PRIVATE LINKER:-sectcreate,__TEXT,__info_plist,${APPLE_PLIST_FILE})
endif()

foreach(flag IN LISTS SUNSHINE_COMPILE_OPTIONS)
    list(APPEND SUNSHINE_COMPILE_OPTIONS_CUDA "$<$<COMPILE_LANGUAGE:CUDA>:--compiler-options=${flag}>")
endforeach()

target_compile_options(sunshine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SUNSHINE_COMPILE_OPTIONS}>;$<$<COMPILE_LANGUAGE:CUDA>:${SUNSHINE_COMPILE_OPTIONS_CUDA};-std=c++17>)  # cmake-lint: disable=C0301

# CPACK / Packaging

# Common options
set(CPACK_PACKAGE_NAME "Sunshine")
set(CPACK_PACKAGE_VENDOR "LizardByte")
set(CPACK_PACKAGE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/cpack_artifacts)
set(CPACK_PACKAGE_CONTACT "https://app.lizardbyte.dev")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "https://github.com/LizardByte")
set(CPACK_PACKAGE_DESCRIPTION ${CMAKE_PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_HOMEPAGE_URL ${CMAKE_PROJECT_HOMEPAGE_URL})
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_ICON ${PROJECT_SOURCE_DIR}/sunshine.png)
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_STRIP_FILES YES)

# install npm modules
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/node_modules"
        DESTINATION "${SUNSHINE_ASSETS_DIR}/web")

# Platform specific options
if(WIN32) # see options at: https://cmake.org/cmake/help/latest/cpack_gen/nsis.html
    install(TARGETS sunshine RUNTIME DESTINATION "." COMPONENT application)

    # Hardening: include zlib1.dll (loaded via LoadLibrary() in openssl's libcrypto.a)
    install(FILES "${ZLIB}" DESTINATION "." COMPONENT application)

    # Adding tools
    install(TARGETS dxgi-info RUNTIME DESTINATION "tools" COMPONENT dxgi)
    install(TARGETS audio-info RUNTIME DESTINATION "tools" COMPONENT audio)
    install(TARGETS sunshinesvc RUNTIME DESTINATION "tools" COMPONENT sunshinesvc)

    # Mandatory tools
    install(TARGETS ddprobe RUNTIME DESTINATION "tools" COMPONENT application)

    # scripts
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/firewall/"
            DESTINATION "scripts"
            COMPONENT firewall)
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/service/"
            DESTINATION "scripts"
            COMPONENT service)
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/migration/"
            DESTINATION "scripts"
            COMPONENT assets)

    # Sunshine assets
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}"
            COMPONENT assets)
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}"
            COMPONENT assets)

    # set(CPACK_NSIS_MUI_HEADERIMAGE "") # TODO: image should be 150x57 bmp
    set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\sunshine.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT__DIR}\\\\${PROJECT_EXE}")
    # The name of the directory that will be created in C:/Program files/
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")

    # Extra install commands
    # Restores permissions on the install directory
    # Migrates config files from the root into the new config folder
    # Sets permissions on the config folder so that we can write in it
    # Install service
    SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
            "${CPACK_NSIS_EXTRA_INSTALL_COMMANDS}
        IfSilent +2 0
        ExecShell 'open' 'https://sunshinestream.readthedocs.io/'
        nsExec::ExecToLog 'icacls \\\"$INSTDIR\\\" /reset'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\migrate-config.bat\\\"'
        nsExec::ExecToLog 'icacls \\\"$INSTDIR\\\\config\\\" /grant:r Users:\\\(OI\\\)\\\(CI\\\)\\\(F\\\)'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\add-firewall-rule.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\install-service.bat\\\"'
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to install ViGEmBus? This is REQUIRED for gamepad support while streaming.' \
            /SD IDNO IDNO NoController
            ExecShell 'open' 'https://github.com/ViGEm/ViGEmBus/releases/latest'; \
                skipped if no
        NoController:
        ")

    # Extra uninstall commands
    # Uninstall service
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS}
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\delete-firewall-rule.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\uninstall-service.bat\\\"'
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove $INSTDIR (this includes the configuration, cover images, and settings)?' \
            /SD IDNO IDNO NoDelete
            RMDir /r \\\"$INSTDIR\\\"; skipped if no
        NoDelete:
        ")

    # Adding an option for the start menu and PATH
    # TODO: it asks to add it to the PATH but is not working https://gitlab.kitware.com/cmake/cmake/-/issues/15635
    set(CPACK_NSIS_MODIFY_PATH "OFF")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    # This will be shown on the installed apps Windows settings
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_PROJECT_NAME}.exe")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "${CPACK_NSIS_CREATE_ICONS_EXTRA}
        CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${CMAKE_PROJECT_NAME} (Foreground Mode).lnk' \
            '\$INSTDIR\\\\${CMAKE_PROJECT_NAME}.exe'
        ")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "${CPACK_NSIS_DELETE_ICONS_EXTRA}
        Delete '\$SMPROGRAMS\\\\$MUI_TEMP\\\\${CMAKE_PROJECT_NAME} (Foreground Mode).lnk'
        ")

    # Checking for previous installed versions
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")

    set(CPACK_NSIS_HELP_LINK "https://sunshinestream.readthedocs.io/about/installation.html")
    set(CPACK_NSIS_URL_INFO_ABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_CONTACT "${CMAKE_PROJECT_HOMEPAGE_URL}/support")

    set(CPACK_NSIS_MENU_LINKS
            "https://sunshinestream.readthedocs.io" "Sunshine documentation"
            "https://app.lizardbyte.dev" "LizardByte Web Site"
            "https://app.lizardbyte.dev/support" "LizardByte Support")

    # Setting components groups and dependencies
    # sunshine binary
    set(CPACK_COMPONENT_APPLICATION_DISPLAY_NAME "${CMAKE_PROJECT_NAME}")
    set(CPACK_COMPONENT_APPLICATION_DESCRIPTION "${CMAKE_PROJECT_NAME} main application.")
    set(CPACK_COMPONENT_APPLICATION_GROUP "core")
    set(CPACK_COMPONENT_APPLICATION_REQUIRED true)
    set(CPACK_COMPONENT_APPLICATION_DEPENDS assets)

    # assets
    set(CPACK_COMPONENT_ASSETS_DISPLAY_NAME "assets")
    set(CPACK_COMPONENT_ASSETS_DESCRIPTION "Shaders, default box art, and web ui.")
    set(CPACK_COMPONENT_ASSETS_GROUP "core")
    set(CPACK_COMPONENT_ASSETS_REQUIRED true)

    # audio tool
    set(CPACK_COMPONENT_AUDIO_DISPLAY_NAME "audio-info")
    set(CPACK_COMPONENT_AUDIO_DESCRIPTION "CLI tool providing information about sound devices.")
    set(CPACK_COMPONENT_AUDIO_GROUP "tools")

    # display tool
    set(CPACK_COMPONENT_DXGI_DISPLAY_NAME "dxgi-info")
    set(CPACK_COMPONENT_DXGI_DESCRIPTION "CLI tool providing information about graphics cards and displays.")
    set(CPACK_COMPONENT_DXGI_GROUP "tools")

    # service
    set(CPACK_COMPONENT_SUNSHINESVC_DISPLAY_NAME "sunshinesvc")
    set(CPACK_COMPONENT_SUNSHINESVC_DESCRIPTION "CLI tool providing ability to enable/disable the Sunshine service.")
    set(CPACK_COMPONENT_SUNSHINESVC_GROUP "tools")

    # service scripts
    set(CPACK_COMPONENT_SERVICE_DISPLAY_NAME "service-scripts")
    set(CPACK_COMPONENT_SERVICE_DESCRIPTION "Scripts to enable/disable the service.")
    set(CPACK_COMPONENT_SERVICE_GROUP "scripts")
    set(CPACK_COMPONENT_SERVICE_DEPENDS sunshinesvc)

    # firewall scripts
    set(CPACK_COMPONENT_FIREWALL_DISPLAY_NAME "firewall-scripts")
    set(CPACK_COMPONENT_FIREWALL_DESCRIPTION "Scripts to enable or disable firewall rules.")
    set(CPACK_COMPONENT_FIREWALL_GROUP "scripts")
endif()
if(APPLE)
    # TODO: bundle doesn't produce a valid .app use cpack -G DragNDrop
    set(CPACK_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
    set(CPACK_BUNDLE_PLIST "${APPLE_PLIST_FILE}")
    set(CPACK_BUNDLE_ICON "${PROJECT_SOURCE_DIR}/sunshine.icns")
    # set(CPACK_BUNDLE_STARTUP_COMMAND "${INSTALL_RUNTIME_DIR}/sunshine")
endif()
if(APPLE AND SUNSHINE_MACOS_PACKAGE)  # TODO
    set(MAC_PREFIX "${CMAKE_PROJECT_NAME}.app/Contents")
    set(INSTALL_RUNTIME_DIR "${MAC_PREFIX}/MacOS")

    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")

    install(TARGETS sunshine
            BUNDLE DESTINATION . COMPONENT Runtime
            RUNTIME DESTINATION ${INSTALL_RUNTIME_DIR} COMPONENT Runtime)
elseif(UNIX)
    # Installation destination dir
    set(CPACK_SET_DESTDIR true)
    if(NOT CMAKE_INSTALL_PREFIX)
        set(CMAKE_INSTALL_PREFIX "/usr/share/sunshine")
    endif()

    install(TARGETS sunshine RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")

    if(APPLE)
        install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
                DESTINATION "${SUNSHINE_ASSETS_DIR}")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/misc/uninstall_pkg.sh"
                DESTINATION "${SUNSHINE_ASSETS_DIR}")
    else()
        install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
                DESTINATION "${SUNSHINE_ASSETS_DIR}")
        if(${SUNSHINE_CONFIGURE_APPIMAGE} OR ${SUNSHINE_CONFIGURE_FLATPAK})
            install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/85-sunshine.rules"
                    DESTINATION "${SUNSHINE_ASSETS_DIR}/udev/rules.d")
            install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
                    DESTINATION "${SUNSHINE_ASSETS_DIR}/systemd/user")
        else()
            install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/85-sunshine.rules"
                    DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d")
            install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
                    DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/systemd/user")
        endif()

        # Post install
        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")
        set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")

        # Dependencies
        set(CPACK_DEB_COMPONENT_INSTALL ON)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
                libboost-filesystem1.67.0 | libboost-filesystem1.71.0 | libboost-filesystem1.74.0, \
                libboost-log1.67.0 | libboost-log1.71.0 | libboost-log1.74.0, \
                libboost-program-options1.67.0 | libboost-program-options1.71.0 | libboost-program-options1.74.0, \
                libboost-thread1.67.0 | libboost-thread1.71.0 | libboost-thread1.74.0, \
                libcap2, \
                libcurl4, \
                libdrm2, \
                libevdev2, \
                libopus0, \
                libpulse0, \
                libxcb-shm0, \
                libxcb-xfixes0, \
                libxtst6, \
                openssl")
        set(CPACK_RPM_PACKAGE_REQUIRES "\
                boost-filesystem >= 1.67.0, \
                boost-log >= 1.67.0, \
                boost-program-options >= 1.67.0, \
                boost-thread >= 1.67.0, \
                libcap >= 2.22, \
                libcurl >= 7.0, \
                libdrm >= 2.4.97, \
                libevdev >= 1.5.6, \
                libopusenc >= 0.2.1, \
                libxcb >= 1.13, \
                libXtst >= 1.2.3, \
                openssl >= 1.1, \
                pulseaudio-libs >= 10.0")
        # This should automatically figure out dependencies, doesn't work with the current config
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    endif()
endif()

include(CPack)
