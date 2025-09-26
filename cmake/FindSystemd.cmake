# - Try to find Systemd
# Once done this will define
#
# SYSTEMD_FOUND - system has systemd
# SYSTEMD_USER_UNIT_INSTALL_DIR - the systemd system unit install directory
# SYSTEMD_SYSTEM_UNIT_INSTALL_DIR - the systemd user unit install directory
# SYSTEMD_MODULES_LOAD_DIR - the systemd modules-load.d directory

IF (NOT WIN32)

    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SYSTEMD "systemd")
    endif()

    if (SYSTEMD_FOUND)
        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=systemd_user_unit_dir systemd
            OUTPUT_STRIP_TRAILING_WHITESPACE
            OUTPUT_VARIABLE SYSTEMD_USER_UNIT_INSTALL_DIR)

        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=systemd_system_unit_dir systemd
            OUTPUT_STRIP_TRAILING_WHITESPACE
            OUTPUT_VARIABLE SYSTEMD_SYSTEM_UNIT_INSTALL_DIR)

        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=modules_load_dir systemd
            OUTPUT_STRIP_TRAILING_WHITESPACE
            OUTPUT_VARIABLE SYSTEMD_MODULES_LOAD_DIR)

        mark_as_advanced(SYSTEMD_USER_UNIT_INSTALL_DIR SYSTEMD_SYSTEM_UNIT_INSTALL_DIR SYSTEMD_MODULES_LOAD_DIR)

    endif ()

ENDIF ()
