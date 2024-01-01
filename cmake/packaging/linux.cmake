# linux specific packaging

install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
        DESTINATION "${SUNSHINE_ASSETS_DIR}")
if(${SUNSHINE_BUILD_APPIMAGE} OR ${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/85-sunshine.rules"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/udev/rules.d")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/systemd/user")
else()
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/85-sunshine.rules"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/udev/rules.d")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/user")
endif()

# Post install
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")

# Dependencies
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
            ${CPACK_DEB_PLATFORM_PACKAGE_DEPENDS} \
            libboost-filesystem${Boost_VERSION}, \
            libboost-locale${Boost_VERSION}, \
            libboost-log${Boost_VERSION}, \
            libboost-program-options${Boost_VERSION}, \
            libcap2, \
            libcurl4, \
            libdrm2, \
            libevdev2, \
            libnuma1, \
            libopus0, \
            libpulse0, \
            libva2, \
            libva-drm2, \
            libvdpau1, \
            libwayland-client0, \
            libx11-6, \
            openssl | libssl3")
set(CPACK_RPM_PACKAGE_REQUIRES "\
            ${CPACK_RPM_PLATFORM_PACKAGE_REQUIRES} \
            boost-filesystem >= ${Boost_VERSION}, \
            boost-locale >= ${Boost_VERSION}, \
            boost-log >= ${Boost_VERSION}, \
            boost-program-options >= ${Boost_VERSION}, \
            libcap >= 2.22, \
            libcurl >= 7.0, \
            libdrm >= 2.4.97, \
            libevdev >= 1.5.6, \
            libopusenc >= 0.2.1, \
            libva >= 2.14.0, \
            libvdpau >= 1.5, \
            libwayland-client >= 1.20.0, \
            libX11 >= 1.7.3.1, \
            numactl-libs >= 2.0.14, \
            openssl >= 3.0.2, \
            pulseaudio-libs >= 10.0")

# This should automatically figure out dependencies, doesn't work with the current config
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)

# application icon
install(FILES "${CMAKE_SOURCE_DIR}/sunshine.svg"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")

# tray icon
if(${SUNSHINE_TRAY} STREQUAL 1)
    install(FILES "${CMAKE_SOURCE_DIR}/sunshine.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status"
            RENAME "sunshine-tray.svg")
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-playing.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status")
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-pausing.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status")
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/common/assets/web/public/images/sunshine-locked.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/status")

    set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
                    ${CPACK_DEBIAN_PACKAGE_DEPENDS}, \
                    libayatana-appindicator3-1, \
                    libnotify4")
    set(CPACK_RPM_PACKAGE_REQUIRES "\
                    ${CPACK_RPM_PACKAGE_REQUIRES}, \
                    libappindicator-gtk3 >= 12.10.0")
endif()

# desktop file
# todo - validate desktop files with `desktop-file-validate`
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
if(NOT ${SUNSHINE_BUILD_APPIMAGE})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine_terminal.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
endif()
if(${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine_kms.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
endif()

# metadata file
# todo - validate file with `appstream-util validate-relax`
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.appdata.xml"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
