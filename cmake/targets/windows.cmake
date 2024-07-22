# windows specific target definitions
set_target_properties(sunshine PROPERTIES LINK_SEARCH_START_STATIC 1)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
find_library(ZLIB ZLIB1)
list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        Windowsapp.lib
        Wtsapi32.lib)

#GUI build
add_custom_target(sunshine-control-panel ALL
        WORKING_DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/common/sunshine-control-panel"
        COMMENT "Installing NPM Dependencies and Building the gui"
        COMMAND bash -c \"npm install && npm run build:win\") # cmake-lint: disable=C0301
