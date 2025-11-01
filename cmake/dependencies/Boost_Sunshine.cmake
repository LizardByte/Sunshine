#
# Loads the boost library giving the following priority
# - LizardByte pre-built
# - System package
# - CPM/FetchContent
#
include_guard(GLOBAL)

if(BOOST_USE_STATIC)
    set(BOOST_PREPARED_BINARIES
            "${CMAKE_SOURCE_DIR}/third-party/build-deps/dist/${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

    # check if the directory exists
    if(EXISTS "${BOOST_PREPARED_BINARIES}")
        set(Boost_FOUND TRUE)  # cmake-lint: disable=C0103
        set(Boost_INCLUDE_DIRS  # cmake-lint: disable=C0103
                "$<BUILD_INTERFACE:${BOOST_PREPARED_BINARIES}/include/boost>")
        file(GLOB Boost_LIBRARIES "${BOOST_PREPARED_BINARIES}/lib/libboost_*.a")
        set(PREPARED_BOOST_USED TRUE)

        # Create Boost::headers target
        add_library(Boost::headers INTERFACE IMPORTED)
        set_target_properties(Boost::headers PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${BOOST_PREPARED_BINARIES}/include/boost"
        )
    else()
        message(WARNING
                "Boost pre-compiled binaries not found at ${BOOST_PREPARED_BINARIES}. \
                Please consider contributing to the LizardByte/build-deps repository.")
    endif()
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
