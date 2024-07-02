if(UNIX)
    if(${SUNSHINE_CONFIGURE_HOMEBREW})
        configure_file(packaging/sunshine.rb sunshine.rb @ONLY)
    endif()
endif()

if(APPLE)
    if(${SUNSHINE_CONFIGURE_PORTFILE})
        configure_file(packaging/macos/Portfile Portfile @ONLY)
    endif()
elseif(UNIX)
    # configure the .desktop file
    set(SUNSHINE_DESKTOP_ICON "sunshine.svg")
    if(${SUNSHINE_BUILD_APPIMAGE})
        configure_file(packaging/linux/AppImage/sunshine.desktop sunshine.desktop @ONLY)
    elseif(${SUNSHINE_BUILD_FLATPAK})
        set(SUNSHINE_DESKTOP_ICON "${PROJECT_FQDN}.svg")
        configure_file(packaging/linux/flatpak/sunshine.desktop sunshine.desktop @ONLY)
        configure_file(packaging/linux/flatpak/sunshine_kms.desktop sunshine_kms.desktop @ONLY)
        configure_file(packaging/linux/sunshine_terminal.desktop sunshine_terminal.desktop @ONLY)
        configure_file(packaging/linux/flatpak/${PROJECT_FQDN}.metainfo.xml
                ${PROJECT_FQDN}.metainfo.xml @ONLY)
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
        configure_file(packaging/linux/Arch/sunshine.install sunshine.install @ONLY)
    endif()

    # configure the flatpak manifest
    if(${SUNSHINE_CONFIGURE_FLATPAK_MAN})
        configure_file(packaging/linux/flatpak/${PROJECT_FQDN}.yml ${PROJECT_FQDN}.yml @ONLY)
        file(COPY packaging/linux/flatpak/deps/ DESTINATION ${CMAKE_BINARY_DIR})
        file(COPY packaging/linux/flatpak/modules DESTINATION ${CMAKE_BINARY_DIR})
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
