if (SUNSHINE_BUILD_HOMEBREW)
    target_link_options(sunshine PRIVATE LINKER:-sectcreate,__TEXT,__info_plist,${APPLE_PLIST_FILE})
else()
    # .app build
    set_target_properties(sunshine PROPERTIES
            OUTPUT_NAME "${CMAKE_PROJECT_NAME}"
            MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${PROJECT_FQDN}"
            MACOSX_BUNDLE_INFO_PLIST "${APPLE_PLIST_FILE}"
            MACOSX_BUNDLE_ICON_FILE "sunshine.icns"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

    # Populate bundle resources in the build tree for local runs.
    set(_bundle_resources_dir "$<TARGET_FILE_DIR:sunshine>/../Resources")
    add_custom_command(TARGET sunshine POST_BUILD
            COMMENT "Copying bundle resources to build tree"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bundle_resources_dir}"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_BINARY_DIR}/assets" "${_bundle_resources_dir}/assets"
            VERBATIM)
endif()

# Tell linker to dynamically load these symbols at runtime, in case they're unavailable:
target_link_options(sunshine PRIVATE -Wl,-U,_CGPreflightScreenCaptureAccess -Wl,-U,_CGRequestScreenCaptureAccess)
