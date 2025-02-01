#
# Loads the libevdev library giving the priority to the system package first, with a fallback to ExternalProject
#
include_guard(GLOBAL)

set(LIBEVDEV_VERSION libevdev-1.13.2)

pkg_check_modules(PC_EVDEV libevdev)
if(PC_EVDEV_FOUND)
    find_path(EVDEV_INCLUDE_DIR libevdev/libevdev.h
            HINTS ${PC_EVDEV_INCLUDE_DIRS} ${PC_EVDEV_INCLUDEDIR})
    find_library(EVDEV_LIBRARY
            NAMES evdev libevdev)
else()
    include(ExternalProject)

    ExternalProject_Add(libevdev
            URL http://www.freedesktop.org/software/libevdev/${LIBEVDEV_VERSION}.tar.xz
            PREFIX ${LIBEVDEV_VERSION}
            CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
            BUILD_COMMAND "make"
            INSTALL_COMMAND ""
    )

    ExternalProject_Get_Property(libevdev SOURCE_DIR)
    message(STATUS "libevdev source dir: ${SOURCE_DIR}")
    set(EVDEV_INCLUDE_DIR "${SOURCE_DIR}")

    ExternalProject_Get_Property(libevdev BINARY_DIR)
    message(STATUS "libevdev binary dir: ${BINARY_DIR}")
    set(EVDEV_LIBRARY "${BINARY_DIR}/libevdev/.libs/libevdev.a")

    # compile libevdev before sunshine
    set(SUNSHINE_TARGET_DEPENDENCIES ${SUNSHINE_TARGET_DEPENDENCIES} libevdev)

    set(EXTERNAL_PROJECT_LIBEVDEV_USED TRUE)
endif()

if(EVDEV_INCLUDE_DIR AND EVDEV_LIBRARY)
    message(STATUS "Found libevdev library: ${EVDEV_LIBRARY}")
    message(STATUS "Found libevdev include directory: ${EVDEV_INCLUDE_DIR}")

    include_directories(SYSTEM ${EVDEV_INCLUDE_DIR})
    list(APPEND PLATFORM_LIBRARIES ${EVDEV_LIBRARY})
else()
    message(FATAL_ERROR "Couldn't find or fetch libevdev")
endif()
