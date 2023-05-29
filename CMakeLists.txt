cmake_minimum_required(VERSION 3.18)

# todo - set version to 0.0.0 once confident in automated versioning
project(Sunshine VERSION 0.20.0
        DESCRIPTION "Sunshine is a self-hosted game stream host for Moonlight."
        HOMEPAGE_URL "https://app.lizardbyte.dev")
set(PROJECT_LONG_DESCRIPTION "Offering low latency, cloud gaming server capabilities with support for AMD, Intel, \
and Nvidia GPUs for hardware encoding. Software encoding is also available. You can connect to Sunshine from any \
Moonlight client on a variety of devices. A web UI is provided to allow configuration, and client pairing, from \
your favorite web browser. Pair from the local server or any mobile device.")

# Update version
# Check if env vars are defined before attempting to access them, variables will be defined even if blank
if((DEFINED ENV{BRANCH}) AND (DEFINED ENV{BUILD_VERSION}) AND (DEFINED ENV{COMMIT}))  # cmake-lint: disable=W0106
    if(($ENV{BRANCH} STREQUAL "master") AND (NOT $ENV{BUILD_VERSION} STREQUAL ""))
        # If BRANCH is "master" and BUILD_VERSION is not empty, then we are building a master branch
        MESSAGE("Got from CI master branch and version $ENV{BUILD_VERSION}")
        set(PROJECT_VERSION $ENV{BUILD_VERSION})
    elseif((DEFINED ENV{BRANCH}) AND (DEFINED ENV{COMMIT}))
        # If BRANCH is set but not BUILD_VERSION we are building nightly, we gather only the commit hash
        MESSAGE("Got from CI $ENV{BRANCH} branch and commit $ENV{COMMIT}")
        set(PROJECT_VERSION ${PROJECT_VERSION}.$ENV{COMMIT})
    endif()
# Generate Sunshine Version based of the git tag
# https://github.com/nocnokneo/cmake-git-versioning-example/blob/master/LICENSE
else()
    find_package(Git)
    if(GIT_EXECUTABLE)
        MESSAGE("${CMAKE_SOURCE_DIR}")
        get_filename_component(SRC_DIR ${CMAKE_SOURCE_DIR} DIRECTORY)
        #Get current Branch
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            #WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE_BRANCH
            RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # Gather current commit
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            #WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
            RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # Check if Dirty
        execute_process(
            COMMAND ${GIT_EXECUTABLE} diff --quiet --exit-code
            #WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_IS_DIRTY
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT GIT_DESCRIBE_ERROR_CODE)
            MESSAGE("Sunshine Branch: ${GIT_DESCRIBE_BRANCH}")
            if(NOT GIT_DESCRIBE_BRANCH STREQUAL "master")
                set(PROJECT_VERSION ${PROJECT_VERSION}.${GIT_DESCRIBE_VERSION})
                MESSAGE("Sunshine Version: ${GIT_DESCRIBE_VERSION}")
            endif()
            if(GIT_IS_DIRTY)
                set(PROJECT_VERSION ${PROJECT_VERSION}.dirty)
                MESSAGE("Git tree is dirty!")
            endif()
        else()
            MESSAGE(ERROR ": Got git error while fetching tags: ${GIT_DESCRIBE_ERROR_CODE}")
        endif()
    else()
        MESSAGE(WARNING ": Git not found, cannot find git version")
    endif()
endif()

# cmake options
option(SUNSHINE_CONFIGURE_APPIMAGE "Configuration specific for AppImage." OFF)
option(SUNSHINE_CONFIGURE_AUR "Configure files required for AUR." OFF)
option(SUNSHINE_CONFIGURE_FLATPAK_MAN "Configure manifest file required for Flatpak build." OFF)
option(SUNSHINE_CONFIGURE_FLATPAK "Configuration specific for Flatpak." OFF)
option(SUNSHINE_CONFIGURE_PORTFILE "Configure macOS Portfile." OFF)
option(SUNSHINE_CONFIGURE_ONLY "Configure special files only, then exit." OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

if(${SUNSHINE_CONFIGURE_APPIMAGE})
    configure_file(${CMAKE_SOURCE_DIR}/packaging/linux/sunshine.desktop sunshine.desktop @ONLY)
elseif(${SUNSHINE_CONFIGURE_AUR})
    configure_file(${CMAKE_SOURCE_DIR}/packaging/linux/aur/PKGBUILD PKGBUILD @ONLY)
elseif(${SUNSHINE_CONFIGURE_FLATPAK_MAN})
    configure_file(${CMAKE_SOURCE_DIR}/packaging/linux/flatpak/dev.lizardbyte.sunshine.yml
            dev.lizardbyte.sunshine.yml @ONLY)
elseif(${SUNSHINE_CONFIGURE_PORTFILE})
    configure_file(${CMAKE_SOURCE_DIR}/packaging/macos/Portfile Portfile @ONLY)
endif()

# return if configure only is set
if(${SUNSHINE_CONFIGURE_ONLY})
    return()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SUNSHINE_LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

# build sunshine
add_subdirectory(${CMAKE_SOURCE_DIR}/src)

# build tests
add_subdirectory(${CMAKE_SOURCE_DIR}/tests)

# build googletest
add_subdirectory(${SUNSHINE_LIB_DIR}/googletest)
