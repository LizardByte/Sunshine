# unix specific target definitions
# put anything here that applies to both linux and macos

#WebUI build
add_custom_target(web-ui ALL
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Installing NPM Dependencies and Building the Web UI"
        COMMAND sh -c \"npm install && SUNSHINE_BUILD_HOMEBREW=${NPM_BUILD_HOMEBREW} SUNSHINE_SOURCE_ASSETS_DIR=${NPM_SOURCE_ASSETS_DIR} SUNSHINE_ASSETS_DIR=${NPM_ASSETS_DIR} npm run build\")  # cmake-lint: disable=C0301
