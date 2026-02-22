if (SUNSHINE_BUILD_HOMEBREW)
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")

    # copy assets to build directory, for running without install
    file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
         DESTINATION "${CMAKE_BINARY_DIR}/assets")
else()
    # .app build
    set(CODESIGN_IDENTITY "" CACHE STRING "Codesign identity, e.g. 'Developer ID Application: Name (TEAMID)'")

    # Build an .app
    set(CMAKE_MACOSX_BUNDLE YES)

    set(MAC_BUNDLE_NAME "${CMAKE_PROJECT_NAME}.app")
    set(MAC_BUNDLE_CONTENTS "${MAC_BUNDLE_NAME}/Contents")
    set(MAC_BUNDLE_RESOURCES "${MAC_BUNDLE_CONTENTS}/Resources")

    install(TARGETS sunshine
        BUNDLE DESTINATION .
        COMPONENT Runtime)

    install(FILES "${APPLE_PLIST_FILE}"
            DESTINATION "${MAC_BUNDLE_CONTENTS}"
            COMPONENT Runtime)

    install(FILES "${PROJECT_SOURCE_DIR}/src_assets/macos/build/sunshine.icns"
            DESTINATION "${MAC_BUNDLE_RESOURCES}"
            COMPONENT Runtime)

    # macOS-specific assets (apps.json, etc.)
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
            DESTINATION "${MAC_BUNDLE_RESOURCES}/assets"
            COMPONENT Runtime
            PATTERN ".DS_Store" EXCLUDE
            PATTERN "._*" EXCLUDE)

    # Pull in non-system dylibs for a self-contained .app
    install(CODE "
        set(_app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_NAME}.app\")

        message(STATUS \"Running fixup_bundle for: \${_app}\")
        include(BundleUtilities)
        set(BU_CHMOD_BUNDLE_ITEMS TRUE)
        fixup_bundle(\"\${_app}\" \"\" \"\")

        # Remove Finder/resource-fork metadata that breaks codesign.
        execute_process(COMMAND /usr/bin/xattr -rc \"\${_app}\")

        message(STATUS \"removing any existing signatures\")
        execute_process(COMMAND /usr/bin/codesign
            --remove-signature --force --deep
            \"\${_app}\"
            RESULT_VARIABLE rc
        )
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR \"codesign failed to remove existing signatures\")
        endif()

        # SHOULD_SIGN is set only when github.event_name == push or when manually building
        if(\$ENV{SHOULD_SIGN} EQUAL 1)
          # Sign anything inside Contents/Frameworks
          set(_fw_dir \"\${_app}/Contents/Frameworks\")
          if(EXISTS \"\${_fw_dir}\")
              file(GLOB_RECURSE _sign_items
                  \"\${_fw_dir}/*.framework\"
                  \"\${_fw_dir}/*.dylib\"
              )

              foreach(item IN LISTS _sign_items)
                  execute_process(COMMAND /usr/bin/codesign --verbose=2
                      --sign \"${CODESIGN_IDENTITY}\" \"\${item}\"
                      --force --timestamp --options=runtime
                      RESULT_VARIABLE rc2
                  )
                  if(NOT rc2 EQUAL 0)
                      message(FATAL_ERROR \"codesign failed while signing library: \${item}\")
                  endif()
              endforeach()
          endif()

          # Sign the app last
          execute_process(COMMAND /usr/bin/codesign --verbose=2
              --sign \"${CODESIGN_IDENTITY}\" \"\${_app}\"
              --force --timestamp --options=runtime
              RESULT_VARIABLE rc3
          )
          if(NOT rc3 EQUAL 0)
              message(FATAL_ERROR \"codesign failed while signing .app\")
          endif()

          # Verify
          execute_process(COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2 \"\${_app}\"
              RESULT_VARIABLE rc4
          )
          if(NOT rc4 EQUAL 0)
              message(FATAL_ERROR \"codesign --verify failed\")
          endif()
        endif()
    " COMPONENT Runtime)

    # DragNDrop
    set(CPACK_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
    set(CPACK_BUNDLE_PLIST "${APPLE_PLIST_FILE}")
    set(CPACK_BUNDLE_ICON "${PROJECT_SOURCE_DIR}/src_assets/macos/build/sunshine.icns")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_DMG_BACKGROUND_IMAGE "${PROJECT_SOURCE_DIR}/src_assets/macos/build/sunshine-background-72dpi.jpg")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${PROJECT_SOURCE_DIR}/src_assets/macos/build/dmg-finder-layout.applescript")
endif()
