# load common dependencies
# this file will also load platform specific dependencies

# submodules
# moonlight common library
set(ENET_NO_INSTALL ON CACHE BOOL "Don't install any libraries build for enet")
add_subdirectory(third-party/moonlight-common-c/enet)

# web server
add_subdirectory(third-party/Simple-Web-Server)

# common dependencies
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

# miniupnp
if(SUNSHINE_SYSTEM_MINIUPNP)
    pkg_check_modules(MINIUPNP miniupnpc REQUIRED)

    # Use includedir pkg-config variable rather than MINIUPNP_INCLUDE_DIRS
    # defined above. The latter may be blank, as pkg-config sometimes omits -I
    # flags for directories that are searched by default (e.g. /usr/include),
    # but we need a value to append /miniupnpc to. Normally source files would
    # prefix their #include directives with miniupnpc/, but this does not align
    # with the directory structure of the git submodule used below.
    pkg_get_variable(MINIUPNP_INCLUDE_DIR miniupnpc includedir)
    include_directories(SYSTEM ${MINIUPNP_INCLUDE_DIR}/miniupnpc)
else()
    set(UPNPC_BUILD_SHARED OFF CACHE BOOL "No shared libraries")
    set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Don't build tests for miniupnpc")
    set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Don't build samples for miniupnpc")
    set(UPNPC_NO_INSTALL ON CACHE BOOL "Don't install any libraries build for miniupnpc")
    set(MINIUPNP_LIBRARIES libminiupnpc-static)
    add_subdirectory(third-party/miniupnp/miniupnpc)
    include_directories(SYSTEM third-party/miniupnp/miniupnpc/include)
endif()

# ffmpeg pre-compiled binaries
if(WIN32)
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
        message(FATAL_ERROR "Unsupported system processor:" ${CMAKE_SYSTEM_PROCESSOR})
    endif()
    set(FFMPEG_PLATFORM_LIBRARIES mfplat ole32 strmiids mfuuid vpl)
    set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-deps/ffmpeg/windows-x86_64")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-deps/ffmpeg/macos-x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-deps/ffmpeg/macos-aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "powerpc")
        message(FATAL_ERROR "PowerPC is not supported on macOS")
    else()
        message(FATAL_ERROR "Unsupported system processor:" ${CMAKE_SYSTEM_PROCESSOR})
    endif()
elseif(UNIX)
    set(FFMPEG_PLATFORM_LIBRARIES va va-drm va-x11 vdpau X11)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        list(APPEND FFMPEG_PLATFORM_LIBRARIES mfx)
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-deps/ffmpeg/linux-x86_64")
        set(CPACK_DEB_PLATFORM_PACKAGE_DEPENDS "libmfx1,")
        set(CPACK_RPM_PLATFORM_PACKAGE_REQUIRES "intel-mediasdk >= 22.3.0,")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-deps/ffmpeg/linux-aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64")
        set(FFMPEG_PREPARED_BINARIES "${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-deps/ffmpeg/linux-powerpc64le")
    else()
        message(FATAL_ERROR "Unsupported system processor:" ${CMAKE_SYSTEM_PROCESSOR})
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

# platform specific dependencies
if(WIN32)
    include(${CMAKE_MODULE_PATH}/dependencies/windows.cmake)
elseif(UNIX)
    include(${CMAKE_MODULE_PATH}/dependencies/unix.cmake)

    if(APPLE)
        include(${CMAKE_MODULE_PATH}/dependencies/macos.cmake)
    else()
        include(${CMAKE_MODULE_PATH}/dependencies/linux.cmake)
    endif()
endif()
