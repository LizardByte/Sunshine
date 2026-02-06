# NSIS Packaging
# see options at: https://cmake.org/cmake/help/latest/cpack_gen/nsis.html

set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT__DIR}\\\\${PROJECT_EXE}")

# Extra install commands
# Runs the main setup script which handles all installation tasks
SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_INSTALL_COMMANDS}
        ; Enable detailed logging
        LogSet on
        IfSilent +3 0
        nsExec::ExecToLog \
          'powershell -ExecutionPolicy Bypass \
          -File \\\"$INSTDIR\\\\scripts\\\\sunshine-setup.ps1\\\" -Action install'
        Goto +2
        nsExec::ExecToLog \
          'powershell -ExecutionPolicy Bypass \
          -File \\\"$INSTDIR\\\\scripts\\\\sunshine-setup.ps1\\\" -Action install -Silent'
        install_done:
        ")

# Extra uninstall commands
# Runs the main setup script which handles all uninstallation tasks
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS}
        ; Enable detailed logging
        LogSet on
        nsExec::ExecToLog \
          'powershell -ExecutionPolicy Bypass \
          -File \\\"$INSTDIR\\\\scripts\\\\sunshine-setup.ps1\\\" -Action uninstall'
        MessageBox MB_YESNO|MB_ICONQUESTION \
          'Do you want to remove $INSTDIR (this includes the configuration, cover images, and settings)?' \
          /SD IDNO IDNO no_delete
          RMDir /r \\\"$INSTDIR\\\"; skipped if no
        no_delete:
        ")

# Adding an option for the start menu
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
# This will be shown on the installed apps Windows settings
set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_PROJECT_NAME}.exe")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "${CPACK_NSIS_CREATE_ICONS_EXTRA}
        SetOutPath '\$INSTDIR'
        CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${CMAKE_PROJECT_NAME}.lnk' \
            '\$INSTDIR\\\\${CMAKE_PROJECT_NAME}.exe' '--shortcut'
        ")
set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "${CPACK_NSIS_DELETE_ICONS_EXTRA}
        Delete '\$SMPROGRAMS\\\\$MUI_TEMP\\\\${CMAKE_PROJECT_NAME}.lnk'
        ")

# Checking for previous installed versions
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")

set(CPACK_NSIS_HELP_LINK "https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html")
set(CPACK_NSIS_URL_INFO_ABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_NSIS_CONTACT "${CMAKE_PROJECT_HOMEPAGE_URL}/support")

set(CPACK_NSIS_MENU_LINKS
        "https://docs.lizardbyte.dev/projects/sunshine" "Sunshine documentation"
        "https://app.lizardbyte.dev" "LizardByte Web Site"
        "https://app.lizardbyte.dev/support" "LizardByte Support")
set(CPACK_NSIS_MANIFEST_DPI_AWARE true)
