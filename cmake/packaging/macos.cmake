# macos specific packaging

# todo - bundle doesn't produce a valid .app use cpack -G DragNDrop
set(CPACK_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_BUNDLE_PLIST "${APPLE_PLIST_FILE}")
set(CPACK_BUNDLE_ICON "${PROJECT_SOURCE_DIR}/sunshine.icns")
# set(CPACK_BUNDLE_STARTUP_COMMAND "${INSTALL_RUNTIME_DIR}/sunshine")

if(SUNSHINE_PACKAGE_MACOS)  # todo
    set(MAC_PREFIX "${CMAKE_PROJECT_NAME}.app/Contents")
    set(INSTALL_RUNTIME_DIR "${MAC_PREFIX}/MacOS")

    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")

    install(TARGETS sunshine
            BUNDLE DESTINATION . COMPONENT Runtime
            RUNTIME DESTINATION ${INSTALL_RUNTIME_DIR} COMPONENT Runtime)
else()
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/misc/uninstall_pkg.sh"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")
endif()
