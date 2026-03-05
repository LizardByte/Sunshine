# Copyright 2019-2022, Collabora, Ltd.
#
# SPDX-License-Identifier: BSL-1.0
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
# Original Author:
# 2019-2022 Rylie Pavlik <rylie.pavlik@collabora.com> <rylie@ryliepavlik.com>

#[[.rst:
FindOpus
---------------

Find the opus codec library.

Targets
^^^^^^^

If successful, the following imported target is created

* ``Opus::opus``

Cache variables
^^^^^^^^^^^^^^^

The following cache variable may also be set to assist/control the operation of this module:

``Opus_ROOT_DIR``
 The root to search for opus.

#]]

set(Opus_ROOT_DIR  # cmake-lint: disable=C0103
    "${Opus_ROOT_DIR}"
    CACHE PATH "Root to search for opus")

# Todo: handle in-tree/fetch-content builds?

if(NOT OPUS_FOUND)
    # Look for a CMake config file
    find_package(Opus QUIET NO_MODULE)
endif()

if(TARGET opus)
    # for fetch content/in tree
    set(Opus_LIBRARY opus)  # cmake-lint: disable=C0103
endif()

if(NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        set(_old_prefix_path "${CMAKE_PREFIX_PATH}")
        # So pkg-config uses Opus_ROOT_DIR too.
        if(Opus_ROOT_DIR)
            list(APPEND CMAKE_PREFIX_PATH ${Opus_ROOT_DIR})
        endif()
        pkg_check_modules(PC_opus QUIET opus)
        # Restore
        set(CMAKE_PREFIX_PATH "${_old_prefix_path}")
    endif()
endif()

find_path(
    Opus_INCLUDE_DIR
    NAMES opus/opus.h
    PATHS ${Opus_ROOT_DIR}
    HINTS ${PC_opus_INCLUDE_DIRS} ${OPUS_INCLUDE_DIR} ${OPUS_INCLUDE_DIRS}
    PATH_SUFFIXES include)
find_library(
    Opus_LIBRARY
    NAMES opus
    PATHS ${Opus_ROOT_DIR}
    HINTS ${PC_opus_LIBRARY_DIRS}
    PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Opus REQUIRED_VARS Opus_LIBRARY
                                                     Opus_INCLUDE_DIR)
if(Opus_FOUND)
    if(NOT TARGET Opus::opus)
        if(TARGET ${Opus_LIBRARY})
            # we want an alias
            add_library(Opus::opus ALIAS ${Opus_LIBRARY})
        else()
            # we want an imported target
            add_library(Opus::opus UNKNOWN IMPORTED)

            set_target_properties(
                Opus::opus
                PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${Opus_INCLUDE_DIR}"
                           IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                           IMPORTED_LOCATION ${Opus_LIBRARY})
        endif()
    endif()
    mark_as_advanced(Opus_INCLUDE_DIR Opus_LIBRARY)
endif()
mark_as_advanced(Opus_ROOT_DIR)

include(FeatureSummary)
set_package_properties(
    Opus PROPERTIES
    URL "https://opus-codec.org/"
    DESCRIPTION
        "The reference library implementation for the audio codec of the same name."
)
