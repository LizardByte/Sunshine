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
    set(FFMPEG_EXTRACT_DIR "${FFMPEG_VERSION_DIR}")
    set(FFMPEG_PREPARED_BINARIES "${FFMPEG_EXTRACT_DIR}/ffmpeg")

    # Set the archive filename based on architecture
    if(NOT DEFINED FFMPEG_RELEASE_ASSET_NAME)
        set(FFMPEG_ARCHIVE_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-ffmpeg.tar.gz")
    else()
        set(FFMPEG_ARCHIVE_NAME "${FFMPEG_RELEASE_ASSET_NAME}")
    endif()
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

# Shared packages supply the full static dependency list, including decoders.
# Older packages retain the library list above.
set(_sunshine_ffmpeg_pc "${FFMPEG_PREPARED_BINARIES}/lib/pkgconfig/libavcodec.pc")
set(_sunshine_ffmpeg_relocatable -1)
if(EXISTS "${_sunshine_ffmpeg_pc}")
    file(READ "${_sunshine_ffmpeg_pc}" _sunshine_ffmpeg_metadata LIMIT 512)
    string(FIND "${_sunshine_ffmpeg_metadata}" "prefix=\${pcfiledir}/../.." _sunshine_ffmpeg_relocatable)
endif()
if(_sunshine_ffmpeg_relocatable GREATER_EQUAL 0)
    # Refresh cached paths when the package or system library search policy changes.
    get_cmake_property(_sunshine_cache_variables CACHE_VARIABLES)
    foreach(variable IN LISTS _sunshine_cache_variables)
        if(variable MATCHES "^(pkgcfg_lib_|__pkg_config_checked_)?SUNSHINE_BUNDLED_(FFMPEG|OPUS)")
            unset(${variable} CACHE)
        endif()
    endforeach()
    foreach(variable PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR)
        if(DEFINED ENV{${variable}})
            set(_sunshine_saved_${variable} "$ENV{${variable}}")
        else()
            unset(_sunshine_saved_${variable})
        endif()
    endforeach()
    set(_sunshine_saved_pkg_config_argn "${PKG_CONFIG_ARGN}")
    # The package supplies static codec archives; system libraries keep their normal linkage.
    set(ENV{PKG_CONFIG_PATH} "${FFMPEG_PREPARED_BINARIES}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_LIBDIR} "$ENV{PKG_CONFIG_PATH}")
    unset(ENV{PKG_CONFIG_SYSROOT_DIR})
    set(PKG_CONFIG_ARGN --static)
    if(WIN32)
        set(_sunshine_saved_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SUNSHINE_BUNDLED_FFMPEG REQUIRED IMPORTED_TARGET libcbs libavcodec libswscale libavutil)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Explicit shared compiler runtimes bypass -static-libstdc++/-static-libgcc.
        # Let the C++ driver apply Sunshine's runtime policy from targets/linux.cmake.
        get_target_property(_sunshine_ffmpeg_link_libraries
                PkgConfig::SUNSHINE_BUNDLED_FFMPEG INTERFACE_LINK_LIBRARIES)
        list(FILTER _sunshine_ffmpeg_link_libraries EXCLUDE
                REGEX "(^|/)(lib)?(stdc\\+\\+|gcc_s)(\\.[^/]*)?$")
        set_property(TARGET PkgConfig::SUNSHINE_BUNDLED_FFMPEG PROPERTY
                INTERFACE_LINK_LIBRARIES "${_sunshine_ffmpeg_link_libraries}")
    endif()
    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libopus.a")
        pkg_check_modules(SUNSHINE_BUNDLED_OPUS REQUIRED IMPORTED_TARGET opus)
    endif()
    foreach(variable PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR)
        if(DEFINED _sunshine_saved_${variable})
            set(ENV{${variable}} "${_sunshine_saved_${variable}}")
        else()
            unset(ENV{${variable}})
        endif()
    endforeach()
    set(PKG_CONFIG_ARGN "${_sunshine_saved_pkg_config_argn}")
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${_sunshine_saved_library_suffixes}")
    endif()
    set(FFMPEG_LIBRARIES PkgConfig::SUNSHINE_BUNDLED_FFMPEG)
endif()

set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREPARED_BINARIES}/include")
