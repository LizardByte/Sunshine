# - Try to find Libva
# This module defines the following variables:
#
# * LIBVA_FOUND - The component was found
# * LIBVA_INCLUDE_DIRS - The component include directory
# * LIBVA_LIBRARIES - The component library Libva
# * LIBVA_DRM_LIBRARIES - The component library Libva DRM

# Use pkg-config to get the directories and then use these values in the
# find_path() and find_library() calls
# cmake-format: on

# The core and DRM backend share a private ABI. Use the host's matched libraries
# and headers so libva also retains the host's driver discovery configuration.
set(_libva_saved_ignore_prefix_path "${CMAKE_IGNORE_PREFIX_PATH}")
set(_libva_saved_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
if(FFMPEG_PREPARED_BINARIES)
    list(APPEND CMAKE_IGNORE_PREFIX_PATH "${FFMPEG_PREPARED_BINARIES}")
    # Discard paths cached by builds that selected the bundled libva.
    foreach(variable LIBVA_INCLUDE_DIR LIBVA_LIB LIBVA_DRM_LIB)
        cmake_path(IS_PREFIX FFMPEG_PREPARED_BINARIES "${${variable}}" NORMALIZE _libva_bundled)
        if(_libva_bundled)
            unset(${variable} CACHE)
            unset(${variable})
        endif()
    endforeach()
endif()
set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_SHARED_LIBRARY_SUFFIX}")
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(_LIBVA libva)
    pkg_check_modules(_LIBVA_DRM libva-drm)
endif()

find_path(
        LIBVA_INCLUDE_DIR
        NAMES va/va.h va/va_drm.h
        HINTS ${_LIBVA_INCLUDE_DIRS}
        PATHS /usr/include /usr/local/include /opt/local/include)

find_library(
        LIBVA_LIB
        NAMES va
        HINTS ${_LIBVA_LIBRARY_DIRS}
        PATHS /usr/lib /usr/local/lib /opt/local/lib)

get_filename_component(_libva_library_dir "${LIBVA_LIB}" DIRECTORY)
find_library(
        LIBVA_DRM_LIB
        NAMES va-drm
        PATHS "${_libva_library_dir}"
        NO_DEFAULT_PATH)
set(CMAKE_IGNORE_PREFIX_PATH "${_libva_saved_ignore_prefix_path}")
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_libva_saved_library_suffixes}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libva REQUIRED_VARS LIBVA_INCLUDE_DIR LIBVA_LIB LIBVA_DRM_LIB)
mark_as_advanced(LIBVA_INCLUDE_DIR LIBVA_LIB LIBVA_DRM_LIB)

if(LIBVA_FOUND)
    set(LIBVA_INCLUDE_DIRS ${LIBVA_INCLUDE_DIR})
    set(LIBVA_LIBRARIES ${LIBVA_LIB})
    set(LIBVA_DRM_LIBRARIES ${LIBVA_DRM_LIB})

    if(NOT TARGET Libva::va)
        if(IS_ABSOLUTE "${LIBVA_LIBRARIES}")
            add_library(Libva::va UNKNOWN IMPORTED)
            set_target_properties(Libva::va PROPERTIES IMPORTED_LOCATION "${LIBVA_LIBRARIES}")
        else()
            add_library(Libva::va INTERFACE IMPORTED)
            set_target_properties(Libva::va PROPERTIES IMPORTED_LIBNAME "${LIBVA_LIBRARIES}")
        endif()

        set_target_properties(Libva::va PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBVA_INCLUDE_DIRS}")
    endif()

    if(NOT TARGET Libva::drm)
        if(IS_ABSOLUTE "${LIBVA_DRM_LIBRARIES}")
            add_library(Libva::drm UNKNOWN IMPORTED)
            set_target_properties(Libva::drm PROPERTIES IMPORTED_LOCATION "${LIBVA_DRM_LIBRARIES}")
        else()
            add_library(Libva::drm INTERFACE IMPORTED)
            set_target_properties(Libva::drm PROPERTIES IMPORTED_LIBNAME "${LIBVA_DRM_LIBRARIES}")
        endif()

        set_target_properties(Libva::drm PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBVA_INCLUDE_DIRS}")
        set_target_properties(Libva::drm PROPERTIES INTERFACE_LINK_LIBRARIES Libva::va)
    endif()

endif()
