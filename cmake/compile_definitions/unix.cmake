# unix specific compile definitions
# put anything here that applies to both linux and macos

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${CURL_LIBRARIES})

# add install prefix to assets path if not already there
# Skip prefix addition for absolute paths or development builds
if(NOT SUNSHINE_ASSETS_DIR MATCHES "^/" AND NOT SUNSHINE_ASSETS_DIR MATCHES "^${CMAKE_INSTALL_PREFIX}")
    set(SUNSHINE_ASSETS_DIR "${CMAKE_INSTALL_PREFIX}/${SUNSHINE_ASSETS_DIR}")
endif()
