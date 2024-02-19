# - Try to find Systemd
# Once done this will define
#
# SYSTEMD_FOUND - system has systemd
# SYSTEMD_USER_UNIT_INSTALL_DIR - the systemd system unit install directory
# SYSTEMD_SYSTEM_UNIT_INSTALL_DIR - the systemd user unit install directory

IF (NOT WIN32)

    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SYSTEMD "systemd")
    endif()

    if (SYSTEMD_FOUND)
        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=systemduserunitdir systemd
            OUTPUT_VARIABLE SYSTEMD_USER_UNIT_INSTALL_DIR)

        string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_USER_UNIT_INSTALL_DIR
            "${SYSTEMD_USER_UNIT_INSTALL_DIR}")

        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=systemdsystemunitdir systemd
            OUTPUT_VARIABLE SYSTEMD_SYSTEM_UNIT_INSTALL_DIR)

        string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_SYSTEM_UNIT_INSTALL_DIR
            "${SYSTEMD_SYSTEM_UNIT_INSTALL_DIR}")

        mark_as_advanced(SYSTEMD_USER_UNIT_INSTALL_DIR SYSTEMD_SYSTEM_UNIT_INSTALL_DIR)

    endif ()

ENDIF ()
