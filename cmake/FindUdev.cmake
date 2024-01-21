# - Try to find Udev
# Once done this will define
#
# UDEV_FOUND - system has udev
# UDEV_RULES_INSTALL_DIR - the udev rules install directory

IF (NOT WIN32)

    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(UDEV "udev")
    endif()

    if (UDEV_FOUND)
        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=udevdir udev
            OUTPUT_VARIABLE UDEV_RULES_INSTALL_DIR)

        string(REGEX REPLACE "[ \t\n]+" "" UDEV_RULES_INSTALL_DIR
            "${UDEV_RULES_INSTALL_DIR}")

        set(UDEV_RULES_INSTALL_DIR "${UDEV_RULES_INSTALL_DIR}/rules.d")

        mark_as_advanced(UDEV_RULES_INSTALL_DIR)

    endif ()

ENDIF ()
