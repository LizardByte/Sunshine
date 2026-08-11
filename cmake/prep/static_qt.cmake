# Configure the standard MSYS2 static Qt prefix automatically for Windows builds.

if(WIN32 AND SUNSHINE_ENABLE_TRAY AND SUNSHINE_USE_STATIC_QT AND NOT Qt6_DIR)
    set(_sunshine_static_qt_prefix "")

    # Prefer the prefix exported by an interactive MSYS2 environment.
    if(DEFINED ENV{MINGW_PREFIX} AND NOT "$ENV{MINGW_PREFIX}" STREQUAL "")  # cmake-lint: disable=W0106
        file(TO_CMAKE_PATH "$ENV{MINGW_PREFIX}/qt6-static" _sunshine_static_qt_candidate)
        if(EXISTS "${_sunshine_static_qt_candidate}/lib/cmake/Qt6/Qt6Config.cmake")
            set(_sunshine_static_qt_prefix "${_sunshine_static_qt_candidate}")
        endif()
    endif()

    # IDEs may not inherit MINGW_PREFIX, so derive the toolchain root from the compiler path.
    if(NOT _sunshine_static_qt_prefix)
        get_filename_component(_sunshine_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_sunshine_compiler_prefix "${_sunshine_compiler_bin_dir}" DIRECTORY)
        set(_sunshine_static_qt_candidate "${_sunshine_compiler_prefix}/qt6-static")
        if(EXISTS "${_sunshine_static_qt_candidate}/lib/cmake/Qt6/Qt6Config.cmake")
            set(_sunshine_static_qt_prefix "${_sunshine_static_qt_candidate}")
        endif()
    endif()

    if(_sunshine_static_qt_prefix)
        list(APPEND CMAKE_PREFIX_PATH "${_sunshine_static_qt_prefix}")
        list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
        message(STATUS "Using static Qt prefix: ${_sunshine_static_qt_prefix}")
    endif()

    unset(_sunshine_compiler_bin_dir)
    unset(_sunshine_compiler_prefix)
    unset(_sunshine_static_qt_candidate)
    unset(_sunshine_static_qt_prefix)
endif()
