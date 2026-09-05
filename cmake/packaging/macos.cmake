# macos specific packaging

if (SUNSHINE_BUILD_HOMEBREW)
    install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
            DESTINATION "${SUNSHINE_ASSETS_DIR}")

    # copy assets to build directory, for running without install
    file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/assets/"
         DESTINATION "${CMAKE_BINARY_DIR}/assets")
else()
    # .app build
    set(APPLE_CODESIGN_IDENTITY "" CACHE STRING "Codesign identity, e.g. 'Developer ID Application: Name (TEAMID)'")

    # Build an .app
    set(CMAKE_MACOSX_BUNDLE YES)

    set(MAC_BUNDLE_NAME "${CMAKE_PROJECT_NAME}.app")
    set(MAC_BUNDLE_CONTENTS "${MAC_BUNDLE_NAME}/Contents")
    set(MAC_BUNDLE_RESOURCES "${MAC_BUNDLE_CONTENTS}/Resources")

    install(TARGETS sunshine
        BUNDLE DESTINATION .
        COMPONENT Runtime)

    if(SUNSHINE_ENABLE_TRAY)
        # Import Qt in this directory so its deployment commands and targets are visible here.
        set(_sunshine_module_path "${CMAKE_MODULE_PATH}")
        find_package(Qt6 REQUIRED COMPONENTS Core)
        set(CMAKE_MODULE_PATH "${_sunshine_module_path}")
        unset(_sunshine_module_path)

        qt6_generate_deploy_script(
            TARGET sunshine
            OUTPUT_SCRIPT SUNSHINE_QT_DEPLOY_SCRIPT
            CONTENT "
qt6_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE_NAME:sunshine>.app\"
    NO_APP_STORE_COMPLIANCE
    NO_TRANSLATIONS
    DEPLOY_TOOL_OPTIONS -no-codesign
)")
        install(SCRIPT "${SUNSHINE_QT_DEPLOY_SCRIPT}" COMPONENT Runtime)
    endif()

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

        # Resolve @rpath items that Qt already deployed into the app bundle.
        function(gp_resolve_item_override context item exepath dirs resolved_item_var resolved_var)
          if(\"\${item}\" MATCHES \"^@rpath/(.+)$\")
            set(_embedded_item \"\${_app}/Contents/Frameworks/\${CMAKE_MATCH_1}\")
            if(EXISTS \"\${_embedded_item}\")
              set(\${resolved_item_var} \"\${_embedded_item}\" PARENT_SCOPE)
              set(\${resolved_var} TRUE PARENT_SCOPE)
            endif()
          endif()
        endfunction()

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

        # SHOULD_SIGN is set only when publish_release is true or when manually building
        if(\"\$ENV{SHOULD_SIGN}\" STREQUAL \"true\")
          # Sign bundled frameworks and plugins before signing the app itself.
          set(_fw_dir \"\${_app}/Contents/Frameworks\")
          if(EXISTS \"\${_fw_dir}\")
              # Framework bundles are top-level directories.
              file(GLOB _framework_items
                  LIST_DIRECTORIES true
                  \"\${_fw_dir}/*.framework\"
              )
              # Recursively collect only library files.
              file(GLOB_RECURSE _sign_items
                  \"\${_fw_dir}/*.dylib\"
                  \"\${_app}/Contents/PlugIns/*.dylib\"
              )
              list(APPEND _sign_items \${_framework_items})

              foreach(item IN LISTS _sign_items)
                  execute_process(COMMAND /usr/bin/codesign --verbose=2
                      --sign \"${APPLE_CODESIGN_IDENTITY}\" \"\${item}\"
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
              --sign \"${APPLE_CODESIGN_IDENTITY}\" \"\${_app}\"
              --entitlements \"${APPLE_ENTITLEMENTS_FILE}\"
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
