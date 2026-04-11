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

    # Fetch tags for the build-deps submodule so tag lookups work in CI shallow clones
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}/third-party/build-deps" fetch --tags --depth=1
        OUTPUT_QUIET
        ERROR_QUIET
    )

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
    set(BOOST_COMPONENTS
            headers
            filesystem
            locale
            log
            log_setup
            program_options
            system
            thread
    )

    set(_boost_use_config FALSE)
    file(GLOB BOOST_CONFIG_DIRS LIST_DIRECTORIES TRUE "${BOOST_PREPARED_BINARIES}/lib/cmake/Boost-*")
    file(GLOB BOOST_HEADERS_TARGET_FILES LIST_DIRECTORIES FALSE
            "${BOOST_PREPARED_BINARIES}/lib/cmake/boost_headers-*/boost_headers-targets.cmake")
    if(BOOST_CONFIG_DIRS AND BOOST_HEADERS_TARGET_FILES)
        list(SORT BOOST_CONFIG_DIRS)
        list(REVERSE BOOST_CONFIG_DIRS)
        list(GET BOOST_CONFIG_DIRS 0 BOOST_CONFIG_DIR)
        list(GET BOOST_HEADERS_TARGET_FILES 0 BOOST_HEADERS_TARGET_FILE)

        file(READ "${BOOST_HEADERS_TARGET_FILE}" BOOST_HEADERS_TARGET_CONTENT LIMIT 512)
        if(BOOST_HEADERS_TARGET_CONTENT MATCHES "get_filename_component\\(_IMPORT_PREFIX")
            set(_boost_use_config TRUE)
        endif()
    endif()

    if(_boost_use_config)
        set(Boost_DIR "${BOOST_CONFIG_DIR}")  # cmake-lint: disable=C0103

        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
            cmake_policy(SET CMP0167 NEW)  # Get BoostConfig.cmake from upstream
        endif()

        find_package(Boost CONFIG REQUIRED COMPONENTS ${BOOST_COMPONENTS})
        message(STATUS "Using relocatable Boost package config from ${Boost_DIR}")
    else()
        message(STATUS "Boost package config is not relocatable. Falling back to manual imported targets.")

        add_library(Boost::headers INTERFACE IMPORTED)
        set_target_properties(Boost::headers PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${BOOST_PREPARED_BINARIES}/include"
        )

        if(NOT TARGET Boost::boost)
            add_library(Boost::boost INTERFACE IMPORTED)
            set_target_properties(Boost::boost PROPERTIES
                    INTERFACE_LINK_LIBRARIES "Boost::headers"
            )
        endif()

        foreach(component headers system algorithm asio_core preprocessor scope uuid regex)
            if(NOT TARGET Boost::${component})
                add_library(Boost::${component} INTERFACE IMPORTED)
                set_target_properties(Boost::${component} PROPERTIES
                        INTERFACE_LINK_LIBRARIES "Boost::headers"
                )
            endif()
        endforeach()

        foreach(component atomic chrono date_time filesystem locale log log_setup program_options thread)
            file(GLOB component_libs "${BOOST_PREPARED_BINARIES}/lib/libboost_${component}*.a")
            if(component_libs AND NOT TARGET Boost::${component})
                list(SORT component_libs)
                list(GET component_libs 0 COMPONENT_LIB)
                add_library(Boost::${component} STATIC IMPORTED)
                set_target_properties(Boost::${component} PROPERTIES
                        IMPORTED_LOCATION "${COMPONENT_LIB}"
                        INTERFACE_INCLUDE_DIRECTORIES "${BOOST_PREPARED_BINARIES}/include"
                )
            endif()
        endforeach()

        if(TARGET Boost::filesystem)
            set_target_properties(Boost::filesystem PROPERTIES
                    INTERFACE_LINK_LIBRARIES "Boost::headers;Boost::system"
            )
        endif()
        if(TARGET Boost::locale)
            set(_boost_locale_link_libraries Boost::headers)
            if(UNIX)
                find_package(Iconv REQUIRED)
                find_package(ICU REQUIRED COMPONENTS data i18n uc)
                list(APPEND _boost_locale_link_libraries
                        Iconv::Iconv
                        ICU::data
                        ICU::i18n
                        ICU::uc
                )
            endif()
            set_target_properties(Boost::locale PROPERTIES
                    INTERFACE_LINK_LIBRARIES "${_boost_locale_link_libraries}"
            )
        endif()
        if(TARGET Boost::program_options)
            set_target_properties(Boost::program_options PROPERTIES
                    INTERFACE_LINK_LIBRARIES "Boost::headers"
            )
        endif()
        if(TARGET Boost::thread)
            set(_boost_thread_link_libraries Boost::headers)
            if(TARGET Boost::chrono)
                list(PREPEND _boost_thread_link_libraries Boost::chrono)
            endif()
            set_target_properties(Boost::thread PROPERTIES
                    INTERFACE_LINK_LIBRARIES "${_boost_thread_link_libraries}"
            )
        endif()
        if(TARGET Boost::log)
            set(_boost_log_link_libraries
                    Boost::asio_core
                    Boost::filesystem
                    Boost::headers
                    Boost::regex
                    Boost::system
                    Boost::thread
            )
            foreach(component atomic date_time)
                if(TARGET Boost::${component})
                    list(APPEND _boost_log_link_libraries Boost::${component})
                endif()
            endforeach()
            if(WIN32)
                list(APPEND _boost_log_link_libraries
                        advapi32
                        mswsock
                        psapi
                        secur32
                        synchronization
                        ws2_32
                )
            endif()
            set_target_properties(Boost::log PROPERTIES
                    INTERFACE_LINK_LIBRARIES "${_boost_log_link_libraries}"
            )
        endif()
        if(TARGET Boost::log_setup)
            set_target_properties(Boost::log_setup PROPERTIES
                    INTERFACE_LINK_LIBRARIES "Boost::headers;Boost::log"
            )
        endif()
        unset(_boost_locale_link_libraries)
        unset(_boost_thread_link_libraries)
        unset(_boost_log_link_libraries)

        set(Boost_FOUND TRUE)  # cmake-lint: disable=C0103
    endif()

    set(PREPARED_BOOST_USED TRUE)

    # Set compile definition to indicate we're using prebuilt Boost
    # This is used to conditionally include platform-specific headers
    add_compile_definitions(SUNSHINE_PREBUILT_BOOST)

    if(TARGET Boost::headers AND NOT TARGET Boost::boost)
        add_library(Boost::boost INTERFACE IMPORTED)
        set_target_properties(Boost::boost PROPERTIES
                INTERFACE_LINK_LIBRARIES "Boost::headers"
        )
    endif()

    foreach(component algorithm preprocessor scope uuid)
        if(TARGET Boost::headers AND NOT TARGET Boost::${component})
            add_library(Boost::${component} INTERFACE IMPORTED)
            set_target_properties(Boost::${component} PROPERTIES
                    INTERFACE_LINK_LIBRARIES "Boost::headers"
            )
        endif()
    endforeach()

    set(Boost_INCLUDE_DIRS "${BOOST_PREPARED_BINARIES}/include")  # cmake-lint: disable=C0103
    set(Boost_LIBRARIES  # cmake-lint: disable=C0103
            Boost::filesystem
            Boost::locale
            Boost::log
            Boost::log_setup
            Boost::program_options
            Boost::system
            Boost::thread
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
