# WIX Packaging
# see options at: https://cmake.org/cmake/help/latest/cpack_gen/wix.html

# find dotnet
find_program(DOTNET_EXECUTABLE dotnet HINTS "C:/Program Files/dotnet")

if(NOT DOTNET_EXECUTABLE)
    message(WARNING "Dotnet executable not found, skipping WiX packaging.")
    return()
endif()

set(CPACK_WIX_VERSION 4)
set(WIX_VERSION 4.0.4)
set(WIX_UI_VERSION 4.0.4)  # extension versioning is independent of the WiX version
set(WIX_BUILD_PARENT_DIRECTORY "${CMAKE_BINARY_DIR}/wix_packaging")
set(WIX_BUILD_DIRECTORY "${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/win64/WIX")

# Download and install WiX tools locally in the build directory
set(WIX_TOOL_PATH "${CMAKE_BINARY_DIR}/.wix")
file(MAKE_DIRECTORY ${WIX_TOOL_PATH})

# Install WiX locally using dotnet
execute_process(
        COMMAND ${DOTNET_EXECUTABLE} tool install --tool-path ${WIX_TOOL_PATH} wix --version ${WIX_VERSION}
        ERROR_VARIABLE WIX_INSTALL_OUTPUT
        RESULT_VARIABLE WIX_INSTALL_RESULT
)

if(NOT WIX_INSTALL_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install WiX tools locally.
     WiX packaging may not work correctly, error: ${WIX_INSTALL_OUTPUT}")
endif()

# Install WiX UI Extension
execute_process(
        COMMAND "${WIX_TOOL_PATH}/wix" extension add WixToolset.UI.wixext/${WIX_UI_VERSION}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        ERROR_VARIABLE WIX_UI_INSTALL_OUTPUT
        RESULT_VARIABLE WIX_UI_INSTALL_RESULT
)

if(NOT WIX_UI_INSTALL_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install WiX UI extension, error: ${WIX_UI_INSTALL_OUTPUT}")
endif()

# Install WiX Util Extension
execute_process(
        COMMAND "${WIX_TOOL_PATH}/wix" extension add WixToolset.Util.wixext/${WIX_UI_VERSION}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        ERROR_VARIABLE WIX_UTIL_INSTALL_OUTPUT
        RESULT_VARIABLE WIX_UTIL_INSTALL_RESULT
)

if(NOT WIX_UTIL_INSTALL_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install WiX Util extension, error: ${WIX_UTIL_INSTALL_OUTPUT}")
endif()

# Set WiX-specific variables
set(CPACK_WIX_ROOT "${WIX_TOOL_PATH}")
set(CPACK_WIX_UPGRADE_GUID "512A3D1B-BE16-401B-A0D1-59BBA3942FB8")

# Installer metadata
set(CPACK_WIX_HELP_LINK "https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html")
set(CPACK_WIX_PRODUCT_ICON "${SUNSHINE_ICON_PATH}")
set(CPACK_WIX_PRODUCT_URL "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "LizardByte")

set(CPACK_WIX_EXTENSIONS
        "WixToolset.UI.wixext"
        "WixToolset.Util.wixext"
)

message(STATUS "cpack package directory: ${CPACK_PACKAGE_DIRECTORY}")

# copy custom wxs files to the build directory
file(COPY "${CMAKE_CURRENT_LIST_DIR}/wix_resources/"
        DESTINATION "${WIX_BUILD_PARENT_DIRECTORY}/")

set(CPACK_WIX_EXTRA_SOURCES
        "${WIX_BUILD_PARENT_DIRECTORY}/sunshine-installer.wxs"
)
set(CPACK_WIX_PATCH_FILE
        "${WIX_BUILD_PARENT_DIRECTORY}/patch.xml"
)

# Copy root LICENSE and rename to have .txt extension
file(COPY "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_BINARY_DIR}")
file(RENAME "${CMAKE_BINARY_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/LICENSE.txt")  # cpack will covert this to an RTF if it is txt

# https://cmake.org/cmake/help/latest/cpack_gen/wix.html#variable:CPACK_WIX_ARCHITECTURE
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
    set(CPACK_WIX_ARCHITECTURE "arm64")
else()
    set(CPACK_WIX_ARCHITECTURE "x64")
endif()
