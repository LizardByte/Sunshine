# - Try to find Udev
# Once done this will define
#
# UDEV_FOUND - system has udev
# UDEV_RULES_INSTALL_DIR - the udev rules install directory
# UDEVADM_EXECUTABLE - path to udevadm executable
# UDEV_VERSION - version of udev/systemd

if(NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(UDEV "udev")
    endif()

    if(UDEV_FOUND)
        if(UDEV_VERSION)
            message(STATUS "Found udev/systemd version: ${UDEV_VERSION}")
        else()
            message(WARNING "Could not determine udev/systemd version")
            set(UDEV_VERSION "0")
        endif()

        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=udev_dir udev
            OUTPUT_STRIP_TRAILING_WHITESPACE
            OUTPUT_VARIABLE UDEV_RULES_INSTALL_DIR)

        set(UDEV_RULES_INSTALL_DIR "${UDEV_RULES_INSTALL_DIR}/rules.d")

        mark_as_advanced(UDEV_RULES_INSTALL_DIR)

        # Check if udevadm is available
        find_program(UDEVADM_EXECUTABLE udevadm
            PATHS /usr/bin /bin /usr/sbin /sbin
            DOC "Path to udevadm executable")
        mark_as_advanced(UDEVADM_EXECUTABLE)

        # Handle version requirements
        if(Udev_FIND_VERSION)
            if(UDEV_VERSION VERSION_LESS Udev_FIND_VERSION)
                set(UDEV_FOUND FALSE)
                if(Udev_FIND_REQUIRED)
                    message(FATAL_ERROR "Udev version ${UDEV_VERSION} less than required version ${Udev_FIND_VERSION}")
                else()
                    message(STATUS "Udev version ${UDEV_VERSION} less than required version ${Udev_FIND_VERSION}")
                endif()
            else()
                message(STATUS "Udev version ${UDEV_VERSION} meets requirement (>= ${Udev_FIND_VERSION})")
            endif()
        endif()
    endif()
endif()
