# load common dependencies
# this file will also load platform specific dependencies

# boost, this should be before Simple-Web-Server as it also depends on boost
include(dependencies/Boost_Sunshine)

# submodules
# moonlight common library
set(ENET_NO_INSTALL ON CACHE BOOL "Don't install any libraries built for enet")

# Apply FreeBSD patch to ENet if on FreeBSD
if(FREEBSD)
    set(ENET_PATCH_FILE "${CMAKE_SOURCE_DIR}/patches/enet-freebsd-support.patch")
    set(ENET_SOURCE_FILE "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/enet/unix.c")
    
    # Check if patch needs to be applied
    execute_process(
        COMMAND patch -p0 -N --dry-run -i ${ENET_PATCH_FILE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/enet
        RESULT_VARIABLE PATCH_DRY_RUN_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    
    # Apply patch if it hasn't been applied yet
    if(PATCH_DRY_RUN_RESULT EQUAL 0)
        message(STATUS "Applying FreeBSD patch to ENet")
        execute_process(
            COMMAND patch -p0 -N -i ${ENET_PATCH_FILE}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/enet
            RESULT_VARIABLE PATCH_RESULT
        )
        
        if(NOT PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply FreeBSD patch to ENet")
        endif()
    endif()
endif()

add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/enet")

# web server
add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/Simple-Web-Server")

# libdisplaydevice
add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/libdisplaydevice")

# common dependencies
include("${CMAKE_MODULE_PATH}/dependencies/nlohmann_json.cmake")
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

# miniupnp
pkg_check_modules(MINIUPNP miniupnpc REQUIRED)
include_directories(SYSTEM ${MINIUPNP_INCLUDE_DIRS})

# ffmpeg pre-compiled binaries
if(NOT DEFINED FFMPEG_PREPARED_BINARIES)
    if(WIN32)
        set(FFMPEG_PLATFORM_LIBRARIES mfplat ole32 strmiids mfuuid vpl)
    elseif(FREEBSD)
        # numa is not available on FreeBSD
        set(FFMPEG_PLATFORM_LIBRARIES va va-drm va-x11 X11)
    elseif(UNIX AND NOT APPLE)
        set(FFMPEG_PLATFORM_LIBRARIES numa va va-drm va-x11 X11)
    endif()
    set(FFMPEG_PREPARED_BINARIES
            "${CMAKE_SOURCE_DIR}/third-party/build-deps/dist/${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

    # check if the directory exists
    if(NOT EXISTS "${FFMPEG_PREPARED_BINARIES}")
        message(FATAL_ERROR
                "FFmpeg pre-compiled binaries not found at ${FFMPEG_PREPARED_BINARIES}. \
                Please consider contributing to the LizardByte/build-deps repository. \
                Optionally, you can use the FFMPEG_PREPARED_BINARIES option to specify the path to the \
                system-installed FFmpeg libraries")
    endif()

    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a")
        set(HDR10_PLUS_LIBRARY
                "${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a")
    endif()
    set(FFMPEG_LIBRARIES
            "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a"
            "${FFMPEG_PREPARED_BINARIES}/lib/libswscale.a"
            "${FFMPEG_PREPARED_BINARIES}/lib/libavutil.a"
            "${FFMPEG_PREPARED_BINARIES}/lib/libcbs.a"
            "${FFMPEG_PREPARED_BINARIES}/lib/libSvtAv1Enc.a"
            "${FFMPEG_PREPARED_BINARIES}/lib/libx264.a"
            "${FFMPEG_PREPARED_BINARIES}/lib/libx265.a"
            ${HDR10_PLUS_LIBRARY}
            ${FFMPEG_PLATFORM_LIBRARIES})
else()
    set(FFMPEG_LIBRARIES
        "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a"
        "${FFMPEG_PREPARED_BINARIES}/lib/libswscale.a"
        "${FFMPEG_PREPARED_BINARIES}/lib/libavutil.a"
        "${FFMPEG_PREPARED_BINARIES}/lib/libcbs.a"
        ${FFMPEG_PLATFORM_LIBRARIES})
endif()

set(FFMPEG_INCLUDE_DIRS
        "${FFMPEG_PREPARED_BINARIES}/include")

# platform specific dependencies
if(WIN32)
    include("${CMAKE_MODULE_PATH}/dependencies/windows.cmake")
elseif(UNIX)
    include("${CMAKE_MODULE_PATH}/dependencies/unix.cmake")

    if(APPLE)
        include("${CMAKE_MODULE_PATH}/dependencies/macos.cmake")
    else()
        include("${CMAKE_MODULE_PATH}/dependencies/linux.cmake")
    endif()
endif()
