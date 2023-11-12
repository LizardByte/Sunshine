if (APPLE)
    if(${SUNSHINE_CONFIGURE_PORTFILE})
        configure_file(packaging/macos/Portfile Portfile @ONLY)
    endif()
elseif (UNIX)
    # configure the .desktop file
    if(${SUNSHINE_BUILD_APPIMAGE})
        configure_file(packaging/linux/AppImage/sunshine.desktop sunshine.desktop @ONLY)
    elseif(${SUNSHINE_BUILD_FLATPAK})
        configure_file(packaging/linux/flatpak/sunshine.desktop sunshine.desktop @ONLY)
        configure_file(packaging/linux/flatpak/sunshine_kms.desktop sunshine_kms.desktop @ONLY)
        configure_file(packaging/linux/sunshine_terminal.desktop sunshine_terminal.desktop @ONLY)
    else()
        configure_file(packaging/linux/sunshine.desktop sunshine.desktop @ONLY)
        configure_file(packaging/linux/sunshine_terminal.desktop sunshine_terminal.desktop @ONLY)
    endif()

    # configure metadata file
    configure_file(packaging/linux/sunshine.appdata.xml sunshine.appdata.xml @ONLY)

    # configure service
    configure_file(packaging/linux/sunshine.service.in sunshine.service @ONLY)

    # configure the arch linux pkgbuild
    if(${SUNSHINE_CONFIGURE_PKGBUILD})
        configure_file(packaging/linux/Arch/PKGBUILD PKGBUILD @ONLY)
    endif()

    # configure the flatpak manifest
    if(${SUNSHINE_CONFIGURE_FLATPAK_MAN})
        configure_file(packaging/linux/flatpak/dev.lizardbyte.sunshine.yml dev.lizardbyte.sunshine.yml @ONLY)
    endif()
endif()

# return if configure only is set
if(${SUNSHINE_CONFIGURE_ONLY})
    # message
    message(STATUS "SUNSHINE_CONFIGURE_ONLY: ON, exiting...")
    set(END_BUILD ON)
else()
    set(END_BUILD OFF)
endif()
