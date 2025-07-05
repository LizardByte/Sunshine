# linux specific packaging

install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
        DESTINATION "${SUNSHINE_ASSETS_DIR}")

# copy assets (excluding shaders) to build directory, for running without install
file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
        DESTINATION "${CMAKE_BINARY_DIR}/assets"
        PATTERN "shaders" EXCLUDE)
# use symbolic link for shaders directory
file(CREATE_LINK "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/shaders"
        "${CMAKE_BINARY_DIR}/assets/shaders" COPY_ON_ERROR SYMBOLIC)

if(${SUNSHINE_BUILD_APPIMAGE} OR ${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/60-sunshine.rules"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/udev/rules.d")
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/60-sunshine.conf"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/modules-load.d")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/systemd/user")
else()
    find_package(Systemd)
    find_package(Udev)

    if(UDEV_FOUND)
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/60-sunshine.rules"
                DESTINATION "${UDEV_RULES_INSTALL_DIR}")
    endif()
    if(SYSTEMD_FOUND)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
                DESTINATION "${SYSTEMD_USER_UNIT_INSTALL_DIR}")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/60-sunshine.conf"
                DESTINATION "${SYSTEMD_MODULES_LOAD_DIR}")
    endif()
endif()

# RPM specific
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")

# FreeBSD specific
set(CPACK_FREEBSD_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_FREEBSD_PACKAGE_ORIGIN "misc/${CPACK_PACKAGE_NAME}")
set(CPACK_FREEBSD_PACKAGE_LICENSE "GPLv3")

# Post install
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")

# FreeBSD post install/deinstall scripts
if(FREEBSD)
    # Note: CPack's FreeBSD generator does NOT natively support install/deinstall scripts
    # like CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA or CPACK_RPM_POST_INSTALL_SCRIPT_FILE.
    # This is a known limitation of the CPack FREEBSD generator.
    #
    # Workaround: Use CPACK_POST_BUILD_SCRIPTS to extract the generated .pkg file,
    # add the install/deinstall scripts, and repack the package. This ensures they are
    # recognized as package control scripts rather than installed files.
    set(CPACK_FREEBSD_PACKAGE_SCRIPTS
        "${SUNSHINE_SOURCE_ASSETS_DIR}/bsd/misc/+POST_INSTALL"
        "${SUNSHINE_SOURCE_ASSETS_DIR}/bsd/misc/+PRE_DEINSTALL"
    )
    list(APPEND CPACK_POST_BUILD_SCRIPTS "${CMAKE_MODULE_PATH}/packaging/freebsd_custom_cpack.cmake")
endif()

# Apply setcap for RPM
# https://github.com/coreos/rpm-ostree/discussions/5036#discussioncomment-10291071
set(CPACK_RPM_USER_FILELIST "%caps(cap_sys_admin+p) ${SUNSHINE_EXECUTABLE_PATH}")

# Dependencies
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
            ${CPACK_DEB_PLATFORM_PACKAGE_DEPENDS} \
            debianutils, \
            libcap2, \
            libcurl4, \
            libdrm2, \
            libgbm1, \
            libevdev2, \
            libnuma1, \
            libopus0, \
            libpulse0, \
            libva2, \
            libva-drm2, \
            libwayland-client0, \
            libx11-6, \
            miniupnpc, \
            openssl | libssl3")
set(CPACK_RPM_PACKAGE_REQUIRES "\
            ${CPACK_RPM_PLATFORM_PACKAGE_REQUIRES} \
            libcap >= 2.22, \
            libcurl >= 7.0, \
            libdrm >= 2.4.97, \
            libevdev >= 1.5.6, \
            libopusenc >= 0.2.1, \
            libva >= 2.14.0, \
            libwayland-client >= 1.20.0, \
            libX11 >= 1.7.3.1, \
            mesa-libgbm >= 25.0.7, \
            miniupnpc >= 2.2.4, \
            numactl-libs >= 2.0.14, \
            openssl >= 3.0.2, \
            pulseaudio-libs >= 10.0, \
            which >= 2.21")
list(APPEND CPACK_FREEBSD_PACKAGE_DEPS
        audio/opus
        ftp/curl
        devel/libevdev
        net/avahi
        x11/libX11
        net/miniupnpc
        security/openssl
)

if(NOT BOOST_USE_STATIC)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
                ${CPACK_DEBIAN_PACKAGE_DEPENDS}, \
                libboost-filesystem${Boost_VERSION}, \
                libboost-locale${Boost_VERSION}, \
                libboost-log${Boost_VERSION}, \
                libboost-program-options${Boost_VERSION}")
    set(CPACK_RPM_PACKAGE_REQUIRES "\
                ${CPACK_RPM_PACKAGE_REQUIRES}, \
                boost-filesystem >= ${Boost_VERSION}, \
                boost-locale >= ${Boost_VERSION}, \
                boost-log >= ${Boost_VERSION}, \
                boost-program-options >= ${Boost_VERSION}")
    list(APPEND CPACK_FREEBSD_PACKAGE_DEPS
            devel/boost-libs
    )
endif()

# This should automatically figure out dependencies on packages
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

# application icon
if(NOT ${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${CMAKE_SOURCE_DIR}/sunshine.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")
else()
    install(FILES "${CMAKE_SOURCE_DIR}/sunshine.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
            RENAME "${PROJECT_FQDN}.svg")
endif()

# tray icon
if(${SUNSHINE_TRAY} STREQUAL 1)
    if(NOT ${SUNSHINE_BUILD_FLATPAK})
        install(FILES "${CMAKE_SOURCE_DIR}/sunshine.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status"
                RENAME "sunshine-tray.svg")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-playing.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-pausing.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-locked.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status")
    else()
        # flatpak icons must be prefixed with the app id or they will not be included in the flatpak
        install(FILES "${CMAKE_SOURCE_DIR}/sunshine.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status"
                RENAME "${PROJECT_FQDN}-tray.svg")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-playing.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status"
                RENAME "${PROJECT_FQDN}-playing.svg")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-pausing.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status"
                RENAME "${PROJECT_FQDN}-pausing.svg")
        install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-locked.svg"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status"
                RENAME "${PROJECT_FQDN}-locked.svg")
    endif()

    set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
                    ${CPACK_DEBIAN_PACKAGE_DEPENDS}, \
                    libayatana-appindicator3-1, \
                    libnotify4")
    set(CPACK_RPM_PACKAGE_REQUIRES "\
                    ${CPACK_RPM_PACKAGE_REQUIRES}, \
                    libappindicator-gtk3 >= 12.10.0")
    list(APPEND CPACK_FREEBSD_PACKAGE_DEPS
            devel/libayatana-appindicator
            devel/libnotify
    )
endif()

# desktop file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_FQDN}.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
if(NOT ${SUNSHINE_BUILD_APPIMAGE} AND NOT ${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_FQDN}.terminal.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
endif()

# metadata file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_FQDN}.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
