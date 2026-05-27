#
# Loads FFmpeg pre-compiled binaries from GitHub releases or a user-specified path
#
include_guard(GLOBAL)

# ffmpeg pre-compiled binaries
if(NOT DEFINED FFMPEG_PREPARED_BINARIES)
    # Set platform-specific libraries
    if(WIN32)
        set(FFMPEG_PLATFORM_LIBRARIES mfplat ole32 strmiids mfuuid vpl)
    elseif(FREEBSD)
        # numa is not available on FreeBSD
        set(FFMPEG_PLATFORM_LIBRARIES va va-drm va-x11 X11)
    elseif(UNIX AND NOT APPLE)
        set(FFMPEG_PLATFORM_LIBRARIES numa va va-drm va-x11 X11)
    endif()

    # Determine download location
    set(FFMPEG_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps")

    # Fetch tags for the build-deps submodule so tag lookups work in CI shallow clones
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" fetch --tags --depth=1
        OUTPUT_QUIET
        ERROR_QUIET
    )

    # Get the current commit/tag from the build-deps submodule
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" describe --tags --exact-match
        OUTPUT_VARIABLE FFMPEG_RELEASE_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # If no exact tag match, try to get the commit hash and look for a tag
    if(NOT FFMPEG_RELEASE_TAG)
        execute_process(
            COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" rev-parse HEAD
            OUTPUT_VARIABLE BUILD_DEPS_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        # Try to find a tag that points to this commit
        execute_process(
            COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" tag --points-at ${BUILD_DEPS_COMMIT}
            OUTPUT_VARIABLE FFMPEG_RELEASE_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    # Set GitHub release URL
    set(FFMPEG_GITHUB_REPO "LizardByte/build-deps")
    if(FFMPEG_RELEASE_TAG)
        set(FFMPEG_RELEASE_URL "https://github.com/${FFMPEG_GITHUB_REPO}/releases/download/${FFMPEG_RELEASE_TAG}")
        set(FFMPEG_VERSION_DIR "${FFMPEG_DOWNLOAD_DIR}/ffmpeg-${FFMPEG_RELEASE_TAG}")
        message(STATUS "Using FFmpeg from build-deps tag: ${FFMPEG_RELEASE_TAG}")
    else()
        set(FFMPEG_RELEASE_URL "https://github.com/${FFMPEG_GITHUB_REPO}/releases/latest/download")
        set(FFMPEG_VERSION_DIR "${FFMPEG_DOWNLOAD_DIR}/ffmpeg-latest")
        message(STATUS "Using FFmpeg from latest build-deps release")
    endif()

    # Set extraction directory and prepared binaries path
    set(FFMPEG_EXTRACT_DIR "${FFMPEG_DOWNLOAD_DIR}")
    set(FFMPEG_PREPARED_BINARIES "${FFMPEG_EXTRACT_DIR}/ffmpeg")

    # Set the archive filename based on architecture
    set(FFMPEG_ARCHIVE_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-ffmpeg.tar.gz")
    set(FFMPEG_ARCHIVE_PATH "${FFMPEG_VERSION_DIR}/${FFMPEG_ARCHIVE_NAME}")
    set(FFMPEG_DOWNLOAD_URL "${FFMPEG_RELEASE_URL}/${FFMPEG_ARCHIVE_NAME}")

    # Check if already downloaded and extracted
    if(NOT EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a")
        # Check if we need to download the archive
        if(NOT EXISTS "${FFMPEG_ARCHIVE_PATH}")
            message(STATUS "Downloading FFmpeg binaries from ${FFMPEG_DOWNLOAD_URL}")

            # Download the archive
            file(DOWNLOAD
                "${FFMPEG_DOWNLOAD_URL}"
                "${FFMPEG_ARCHIVE_PATH}"
                SHOW_PROGRESS
                STATUS FFMPEG_DOWNLOAD_STATUS
                TIMEOUT 300
            )

            # Check download status
            list(GET FFMPEG_DOWNLOAD_STATUS 0 FFMPEG_DOWNLOAD_STATUS_CODE)
            list(GET FFMPEG_DOWNLOAD_STATUS 1 FFMPEG_DOWNLOAD_STATUS_MESSAGE)

            if(NOT FFMPEG_DOWNLOAD_STATUS_CODE EQUAL 0)
                message(FATAL_ERROR "Failed to download FFmpeg binaries: ${FFMPEG_DOWNLOAD_STATUS_MESSAGE}")
            endif()
        else()
            message(STATUS "Using cached FFmpeg archive at ${FFMPEG_ARCHIVE_PATH}")
        endif()

        # Extract the archive
        message(STATUS "Extracting FFmpeg binaries to ${FFMPEG_EXTRACT_DIR}")
        file(ARCHIVE_EXTRACT  # cmake-lint: disable=E1126
            INPUT "${FFMPEG_ARCHIVE_PATH}"
            DESTINATION "${FFMPEG_EXTRACT_DIR}"
        )

        # Verify extraction
        if(NOT EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a")
            message(FATAL_ERROR "FFmpeg extraction failed or unexpected directory structure")
        endif()

        message(STATUS "FFmpeg binaries successfully downloaded and extracted")
    else()
        message(STATUS "Using existing FFmpeg binaries at ${FFMPEG_PREPARED_BINARIES}")
    endif()

    # Set FFmpeg libraries
    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a")
        set(HDR10_PLUS_LIBRARY "${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a")
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
        ${FFMPEG_PLATFORM_LIBRARIES}
    )
else()
    # User provided FFMPEG_PREPARED_BINARIES path
    message(STATUS "Using user-specified FFmpeg binaries at ${FFMPEG_PREPARED_BINARIES}")

    # Set platform-specific libraries
    if(NOT DEFINED FFMPEG_PLATFORM_LIBRARIES)
        if(WIN32)
            set(FFMPEG_PLATFORM_LIBRARIES mfplat ole32 strmiids mfuuid vpl)
        elseif(FREEBSD)
            set(FFMPEG_PLATFORM_LIBRARIES va va-drm va-x11 X11)
        elseif(UNIX AND NOT APPLE)
            set(FFMPEG_PLATFORM_LIBRARIES numa va va-drm va-x11 X11)
        endif()
    endif()

    # Set base FFmpeg libraries (always required)
    set(FFMPEG_LIBRARIES
        "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a"
        "${FFMPEG_PREPARED_BINARIES}/lib/libswscale.a"
        "${FFMPEG_PREPARED_BINARIES}/lib/libavutil.a"
        "${FFMPEG_PREPARED_BINARIES}/lib/libcbs.a"
    )

    # Add optional libraries if they exist (e.g., from prebuilt packages)
    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libSvtAv1Enc.a")
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_PREPARED_BINARIES}/lib/libSvtAv1Enc.a")
    endif()
    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libx264.a")
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_PREPARED_BINARIES}/lib/libx264.a")
    endif()
    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libx265.a")
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_PREPARED_BINARIES}/lib/libx265.a")
    endif()
    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a")
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_PREPARED_BINARIES}/lib/libhdr10plus.a")
    endif()

    # Add platform libraries
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_PLATFORM_LIBRARIES})
endif()

set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREPARED_BINARIES}/include")

# Sunshine's src/cbs.cpp uses libavcodec's INTERNAL headers (cbs_h264.h,
# cbs_h2645.h, h2645_parse.h, etc.) which FFmpeg's `make install` does
# not export. Stage the needed internal headers into the dist include
# tree alongside the public ones. Done at configure time so subsequent
# rebuilds don't pay the cost. Limited to a known-good list so we don't
# shadow system headers (FFmpeg has its own "thread.h", "internal.h",
# etc. that collide with libc++ when the entire source tree is on the
# include path).
get_filename_component(_FFMPEG_BINARY_PARENT "${FFMPEG_PREPARED_BINARIES}" DIRECTORY)
set(_FFMPEG_SOURCE_CANDIDATES
    "${_FFMPEG_BINARY_PARENT}/FFmpeg/FFmpeg"
    "${CMAKE_SOURCE_DIR}/third-party/build-deps/build-prores-vt/FFmpeg/FFmpeg"
)
foreach(_candidate ${_FFMPEG_SOURCE_CANDIDATES})
    if(EXISTS "${_candidate}/libavcodec/h2645_parse.h")
        set(_FFMPEG_INTERNAL_HEADERS
            libavcodec/h2645_parse.h
            libavcodec/h2645_sei.h
            libavcodec/h264_sei.h
            libavcodec/hevc/sei.h
            libavcodec/sei.h
            libavcodec/cbs.h
            libavcodec/cbs_internal.h
            libavcodec/cbs_sei.h
            libavcodec/get_bits.h
            libavcodec/golomb.h
            libavcodec/mathops.h
            libavcodec/mpegutils.h
            libavcodec/vlc.h
            libavutil/attributes_internal.h
            libavutil/internal.h
            libavutil/thread.h
            libavutil/timer.h
            libavutil/reverse.h
            libavutil/libm.h
            libavutil/cpu_internal.h
        )
        foreach(_hdr ${_FFMPEG_INTERNAL_HEADERS})
            if(EXISTS "${_candidate}/${_hdr}" AND NOT EXISTS "${FFMPEG_PREPARED_BINARIES}/include/${_hdr}")
                get_filename_component(_hdr_dir "${FFMPEG_PREPARED_BINARIES}/include/${_hdr}" DIRECTORY)
                file(MAKE_DIRECTORY "${_hdr_dir}")
                configure_file("${_candidate}/${_hdr}" "${FFMPEG_PREPARED_BINARIES}/include/${_hdr}" COPYONLY)
            endif()
        endforeach()
        # ffbuild/config.h is needed by libavutil/internal.h. Stage it
        # at the include-path root so the relative #include "config.h"
        # from mathops.h finds it.
        if(EXISTS "${_candidate}/ffbuild/config.h" AND NOT EXISTS "${FFMPEG_PREPARED_BINARIES}/include/config.h")
            configure_file("${_candidate}/ffbuild/config.h" "${FFMPEG_PREPARED_BINARIES}/include/config.h" COPYONLY)
        endif()
        message(STATUS "Sunshine cbs.cpp: staged FFmpeg internal headers from ${_candidate}")
        break()
    endif()
endforeach()
