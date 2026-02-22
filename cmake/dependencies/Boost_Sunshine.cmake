#
# Loads the boost library giving the following priority
# - LizardByte pre-built
# - System package
# - CPM/FetchContent
#
include_guard(GLOBAL)

# boost pre-compiled binaries
if(NOT DEFINED BOOST_PREPARED_BINARIES AND BOOST_USE_STATIC)
    # Determine download location
    set(BOOST_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps")

    # Get the current commit/tag from the build-deps submodule
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" describe --tags --exact-match
        OUTPUT_VARIABLE BOOST_RELEASE_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # If no exact tag match, try to get the commit hash and look for a tag
    if(NOT BOOST_RELEASE_TAG)
        execute_process(
            COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" rev-parse HEAD
            OUTPUT_VARIABLE BUILD_DEPS_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        # Try to find a tag that points to this commit
        execute_process(
            COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" tag --points-at ${BUILD_DEPS_COMMIT}
            OUTPUT_VARIABLE BOOST_RELEASE_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    # Set GitHub release URL
    set(BOOST_GITHUB_REPO "LizardByte/build-deps")
    if(BOOST_RELEASE_TAG)
        set(BOOST_RELEASE_URL "https://github.com/${BOOST_GITHUB_REPO}/releases/download/${BOOST_RELEASE_TAG}")
        set(BOOST_VERSION_DIR "${BOOST_DOWNLOAD_DIR}/boost-${BOOST_RELEASE_TAG}")
        message(STATUS "Using Boost from build-deps tag: ${BOOST_RELEASE_TAG}")
    else()
        set(BOOST_RELEASE_URL "https://github.com/${BOOST_GITHUB_REPO}/releases/latest/download")
        set(BOOST_VERSION_DIR "${BOOST_DOWNLOAD_DIR}/boost-latest")
        message(STATUS "Using Boost from latest build-deps release")
    endif()

    # Set extraction directory and prepared binaries path
    set(BOOST_EXTRACT_DIR "${BOOST_DOWNLOAD_DIR}")
    set(BOOST_PREPARED_BINARIES "${BOOST_EXTRACT_DIR}/boost")

    # Set the archive filename based on architecture
    set(BOOST_ARCHIVE_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-boost.tar.gz")
    set(BOOST_ARCHIVE_PATH "${BOOST_VERSION_DIR}/${BOOST_ARCHIVE_NAME}")
    set(BOOST_DOWNLOAD_URL "${BOOST_RELEASE_URL}/${BOOST_ARCHIVE_NAME}")

    # Check if already downloaded and extracted
    if(NOT EXISTS "${BOOST_PREPARED_BINARIES}/lib")
        # Check if we need to download the archive
        if(NOT EXISTS "${BOOST_ARCHIVE_PATH}")
            message(STATUS "Downloading Boost binaries from ${BOOST_DOWNLOAD_URL}")

            # Download the archive
            file(DOWNLOAD
                "${BOOST_DOWNLOAD_URL}"
                "${BOOST_ARCHIVE_PATH}"
                SHOW_PROGRESS
                STATUS BOOST_DOWNLOAD_STATUS
                TIMEOUT 300
            )

            # Check download status
            list(GET BOOST_DOWNLOAD_STATUS 0 BOOST_DOWNLOAD_STATUS_CODE)
            list(GET BOOST_DOWNLOAD_STATUS 1 BOOST_DOWNLOAD_STATUS_MESSAGE)

            if(NOT BOOST_DOWNLOAD_STATUS_CODE EQUAL 0)
                message(FATAL_ERROR "Failed to download Boost binaries: ${BOOST_DOWNLOAD_STATUS_MESSAGE}")
            endif()
        else()
            message(STATUS "Using cached Boost archive at ${BOOST_ARCHIVE_PATH}")
        endif()

        # Extract the archive
        message(STATUS "Extracting Boost binaries to ${BOOST_EXTRACT_DIR}")
        file(ARCHIVE_EXTRACT  # cmake-lint: disable=E1126
            INPUT "${BOOST_ARCHIVE_PATH}"
            DESTINATION "${BOOST_EXTRACT_DIR}"
        )

        # Verify extraction
        if(NOT EXISTS "${BOOST_PREPARED_BINARIES}/lib")
            message(FATAL_ERROR "Boost extraction failed or unexpected directory structure")
        endif()

        message(STATUS "Boost binaries successfully downloaded and extracted")
    else()
        message(STATUS "Using existing Boost binaries at ${BOOST_PREPARED_BINARIES}")
    endif()
endif()

if(BOOST_PREPARED_BINARIES AND EXISTS "${BOOST_PREPARED_BINARIES}")
    set(Boost_FOUND TRUE)  # cmake-lint: disable=C0103
    set(Boost_INCLUDE_DIRS  # cmake-lint: disable=C0103
            "$<BUILD_INTERFACE:${BOOST_PREPARED_BINARIES}/include/boost>")

    # Define the components we need
    set(BOOST_COMPONENTS
            filesystem
            locale
            log
            program_options
            system
            thread
    )

    # Build the library list from actual components instead of globbing all libraries
    set(Boost_LIBRARIES "")  # cmake-lint: disable=C0103
    foreach(component ${BOOST_COMPONENTS})
        file(GLOB component_libs "${BOOST_PREPARED_BINARIES}/lib/libboost_${component}*.a")
        if(component_libs)
            list(APPEND Boost_LIBRARIES ${component_libs})
        endif()
    endforeach()

    set(PREPARED_BOOST_USED TRUE)

    # Set compile definition to indicate we're using prebuilt Boost
    # This is used to conditionally include platform-specific headers
    add_compile_definitions(SUNSHINE_PREBUILT_BOOST)

    # Create Boost::headers target
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(Boost::headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${BOOST_PREPARED_BINARIES}/include/boost"
    )
endif()

if(NOT Boost_FOUND)
    set(BOOST_COMPONENTS
            filesystem
            locale
            log
            program_options
            system
    )
    # system is not used by Sunshine, but by Simple-Web-Server, added here for convenience

    # algorithm, preprocessor, scope, and uuid are not used by Sunshine,
    # but by libdisplaydevice, added here for convenience
    if(WIN32)
        list(APPEND BOOST_COMPONENTS
                algorithm
                preprocessor
                scope
                uuid
        )
    endif()

    if(BOOST_USE_STATIC)
        set(Boost_USE_STATIC_LIBS ON)  # cmake-lint: disable=C0103
    endif()

    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
        cmake_policy(SET CMP0167 NEW)  # Get BoostConfig.cmake from upstream
    endif()
    find_package(Boost CONFIG ${BOOST_VERSION} EXACT COMPONENTS ${BOOST_COMPONENTS})
    if(NOT Boost_FOUND)
        message(STATUS "Boost v${BOOST_VERSION} package not found in the system. Falling back to CPM.")
        CPMGetPackage(Boost)

        # more components required for compiling boost targets
        list(APPEND BOOST_COMPONENTS
                asio
                crc
                format
                process
                property_tree)

        set(BOOST_ENABLE_CMAKE ON)  # Use experimental superproject to pull library dependencies recursively
        set(BOOST_INCLUDE_LIBRARIES ${BOOST_COMPONENTS})  # Limit boost to the required libraries only
        set(BOOST_SKIP_INSTALL_RULES ON)  # do not install Boost libraries or headers

        set(CPM_BOOST_USED TRUE)

        set(Boost_FOUND TRUE)  # cmake-lint: disable=C0103
        set(Boost_INCLUDE_DIRS  # cmake-lint: disable=C0103
                "$<BUILD_INTERFACE:${Boost_SOURCE_DIR}/libs/headers/include>")

        message(STATUS "Boost_BINARY_DIR: ${Boost_BINARY_DIR}")
        message(STATUS "Boost_SOURCE_DIR: ${Boost_SOURCE_DIR}")

        if(WIN32)
            # Windows build is failing to create .h file in this directory
            file(MAKE_DIRECTORY ${Boost_BINARY_DIR}/libs/log/src/windows)
        endif()

        add_subdirectory(${Boost_SOURCE_DIR} ${Boost_BINARY_DIR} SYSTEM)

        set(Boost_LIBRARIES "")  # cmake-lint: disable=C0103
        foreach(component ${BOOST_COMPONENTS})
            list(APPEND Boost_LIBRARIES "Boost::${component}")
        endforeach()
    endif()
endif()

message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
