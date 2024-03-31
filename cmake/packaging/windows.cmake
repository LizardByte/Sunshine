# windows specific packaging

# see options at: https://cmake.org/cmake/help/latest/cpack_gen/nsis.html
install(TARGETS sunshine RUNTIME DESTINATION "." COMPONENT application)

# Hardening: include zlib1.dll (loaded via LoadLibrary() in openssl's libcrypto.a)
install(FILES "${ZLIB}" DESTINATION "." COMPONENT application)

# Adding tools
install(TARGETS dxgi-info RUNTIME DESTINATION "tools" COMPONENT dxgi)
install(TARGETS audio-info RUNTIME DESTINATION "tools" COMPONENT audio)

# Mandatory tools
install(TARGETS ddprobe RUNTIME DESTINATION "tools" COMPONENT application)
install(TARGETS sunshinesvc RUNTIME DESTINATION "tools" COMPONENT application)

# Mandatory scripts
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/service/"
        DESTINATION "scripts"
        COMPONENT assets)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/migration/"
        DESTINATION "scripts"
        COMPONENT assets)

# Configurable options for the service
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/autostart/"
        DESTINATION "scripts"
        COMPONENT autostart)

# scripts
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/firewall/"
        DESTINATION "scripts"
        COMPONENT firewall)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/gamepad/"
        DESTINATION "scripts"
        COMPONENT gamepad)

# Sunshine assets
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/"
        DESTINATION "${SUNSHINE_ASSETS_DIR}"
        COMPONENT assets)
# copy assets to build directory, for running without install
file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/"
        DESTINATION "${CMAKE_BINARY_DIR}/assets")

# set(CPACK_NSIS_MUI_HEADERIMAGE "") # TODO: image should be 150x57 bmp
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}\\\\sunshine.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT__DIR}\\\\${PROJECT_EXE}")
# The name of the directory that will be created in C:/Program files/
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")

# Extra install commands
# Restores permissions on the install directory
# Migrates config files from the root into the new config folder
# Install service
SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_INSTALL_COMMANDS}
        IfSilent +2 0
        ExecShell 'open' 'https://sunshinestream.readthedocs.io/'
        nsExec::ExecToLog 'icacls \\\"$INSTDIR\\\" /reset'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\migrate-config.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\add-firewall-rule.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\install-gamepad.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\install-service.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\autostart-service.bat\\\"'
        NoController:
        ")

# Extra uninstall commands
# Uninstall service
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS}
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\delete-firewall-rule.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\uninstall-service.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\sunshine.exe\\\" --restore-nvprefs-undo'
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove Virtual Gamepad)?' \
            /SD IDNO IDNO NoGamepad
            nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\uninstall-gamepad.bat\\\"'; skipped if no
        NoGamepad:
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove $INSTDIR (this includes the configuration, cover images, and settings)?' \
            /SD IDNO IDNO NoDelete
            RMDir /r \\\"$INSTDIR\\\"; skipped if no
        NoDelete:
        ")

# Adding an option for the start menu
set(CPACK_NSIS_MODIFY_PATH "OFF")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
# This will be shown on the installed apps Windows settings
set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_PROJECT_NAME}.exe")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "${CPACK_NSIS_CREATE_ICONS_EXTRA}
        CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${CMAKE_PROJECT_NAME}.lnk' \
            '\$INSTDIR\\\\${CMAKE_PROJECT_NAME}.exe' '--shortcut'
        ")
set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "${CPACK_NSIS_DELETE_ICONS_EXTRA}
        Delete '\$SMPROGRAMS\\\\$MUI_TEMP\\\\${CMAKE_PROJECT_NAME}.lnk'
        ")

# Checking for previous installed versions
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")

set(CPACK_NSIS_HELP_LINK "https://sunshinestream.readthedocs.io/en/latest/about/installation.html")
set(CPACK_NSIS_URL_INFO_ABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_NSIS_CONTACT "${CMAKE_PROJECT_HOMEPAGE_URL}/support")

set(CPACK_NSIS_MENU_LINKS
        "https://sunshinestream.readthedocs.io" "Sunshine documentation"
        "https://app.lizardbyte.dev" "LizardByte Web Site"
        "https://app.lizardbyte.dev/support" "LizardByte Support")
set(CPACK_NSIS_MANIFEST_DPI_AWARE true)

# Setting components groups and dependencies
set(CPACK_COMPONENT_GROUP_CORE_EXPANDED true)

# sunshine binary
set(CPACK_COMPONENT_APPLICATION_DISPLAY_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_COMPONENT_APPLICATION_DESCRIPTION "${CMAKE_PROJECT_NAME} main application and required components.")
set(CPACK_COMPONENT_APPLICATION_GROUP "Core")
set(CPACK_COMPONENT_APPLICATION_REQUIRED true)
set(CPACK_COMPONENT_APPLICATION_DEPENDS assets)

# service auto-start script
set(CPACK_COMPONENT_AUTOSTART_DISPLAY_NAME "Launch on Startup")
set(CPACK_COMPONENT_AUTOSTART_DESCRIPTION "If enabled, launches Sunshine automatically on system startup.")
set(CPACK_COMPONENT_AUTOSTART_GROUP "Core")

# assets
set(CPACK_COMPONENT_ASSETS_DISPLAY_NAME "Required Assets")
set(CPACK_COMPONENT_ASSETS_DESCRIPTION "Shaders, default box art, and web UI.")
set(CPACK_COMPONENT_ASSETS_GROUP "Core")
set(CPACK_COMPONENT_ASSETS_REQUIRED true)

# audio tool
set(CPACK_COMPONENT_AUDIO_DISPLAY_NAME "audio-info")
set(CPACK_COMPONENT_AUDIO_DESCRIPTION "CLI tool providing information about sound devices.")
set(CPACK_COMPONENT_AUDIO_GROUP "Tools")

# display tool
set(CPACK_COMPONENT_DXGI_DISPLAY_NAME "dxgi-info")
set(CPACK_COMPONENT_DXGI_DESCRIPTION "CLI tool providing information about graphics cards and displays.")
set(CPACK_COMPONENT_DXGI_GROUP "Tools")

# firewall scripts
set(CPACK_COMPONENT_FIREWALL_DISPLAY_NAME "Add Firewall Exclusions")
set(CPACK_COMPONENT_FIREWALL_DESCRIPTION "Scripts to enable or disable firewall rules.")
set(CPACK_COMPONENT_FIREWALL_GROUP "Scripts")

# gamepad scripts
set(CPACK_COMPONENT_GAMEPAD_DISPLAY_NAME "Virtual Gamepad")
set(CPACK_COMPONENT_GAMEPAD_DESCRIPTION "Scripts to install and uninstall Virtual Gamepad.")
set(CPACK_COMPONENT_GAMEPAD_GROUP "Scripts")
