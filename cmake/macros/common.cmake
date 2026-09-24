# common macros
# this file will also load platform specific macros

# platform specific macros
if(WIN32)
    include(${CMAKE_MODULE_PATH}/macros/windows.cmake)
elseif(UNIX)
    include(${CMAKE_MODULE_PATH}/macros/unix.cmake)

    if(APPLE)
        include(${CMAKE_MODULE_PATH}/macros/macos.cmake)
    else()
        include(${CMAKE_MODULE_PATH}/macros/linux.cmake)
    endif()
endif()

# override find_package function
macro(find_package)  # cmake-lint: disable=C0103
    string(TOLOWER "${ARGV0}" ARGV0_LOWER)
    if(
        (("${ARGV0_LOWER}" STREQUAL "boost") AND DEFINED CPM_BOOST_USED) OR
        (("${ARGV0_LOWER}" STREQUAL "libevdev") AND DEFINED EXTERNAL_PROJECT_LIBEVDEV_USED)
    )
        # Do nothing, as the package has already been fetched
    elseif(("${ARGV0_LOWER}" STREQUAL "boost") AND POLICY CMP0167)
        # Prefer upstream BoostConfig.cmake for compatible system packages on CMake 3.30+.
        cmake_policy(PUSH)
        cmake_policy(SET CMP0167 NEW)
        _find_package(${ARGV})
        cmake_policy(POP)
    else()
        # Call the original find_package function
        _find_package(${ARGV})
    endif()
endmacro()
