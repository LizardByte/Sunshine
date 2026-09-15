#
# Loads the boost library giving priority to a compatible system package, with a fallback to CPM.
#
include_guard(GLOBAL)

set(BOOST_MINIMUM_VERSION "1.89.0")
set(BOOST_COMPONENTS
        filesystem
        log
        program_options
)

if(NOT WIN32)
    list(APPEND BOOST_COMPONENTS locale)
endif()

# algorithm, preprocessor, scope, and uuid are not used by Sunshine, but by libdisplaydevice, added here for convenience
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

find_package(Boost ${BOOST_MINIMUM_VERSION} CONFIG COMPONENTS ${BOOST_COMPONENTS})
if(NOT Boost_FOUND)
    message(STATUS "Boost v${BOOST_MINIMUM_VERSION}+ package not found in the system. Falling back to CPM.")

    # more components required for compiling boost targets
    list(APPEND BOOST_COMPONENTS
            asio
            crc
            format
            process
            property_tree)

    set(BOOST_ENABLE_CMAKE ON)  # Use the Boost superproject to resolve library dependencies recursively
    set(BOOST_INCLUDE_LIBRARIES ${BOOST_COMPONENTS})  # Limit Boost to the required libraries
    set(BOOST_SKIP_INSTALL_RULES ON)  # Do not install Boost libraries or headers

    CPMGetPackage(Boost)
    set(CPM_BOOST_USED TRUE)

    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25.0")
        add_subdirectory(${Boost_SOURCE_DIR} ${Boost_BINARY_DIR} SYSTEM)
    else()
        add_subdirectory(${Boost_SOURCE_DIR} ${Boost_BINARY_DIR})
    endif()

    set(Boost_FOUND TRUE)  # cmake-lint: disable=C0103
    set(Boost_VERSION "${BOOST_VERSION}")  # cmake-lint: disable=C0103
    set(Boost_INCLUDE_DIRS  # cmake-lint: disable=C0103
            "$<BUILD_INTERFACE:${Boost_SOURCE_DIR}/libs/headers/include>")

    if(WIN32)
        # Windows build is failing to create .h file in this directory
        file(MAKE_DIRECTORY ${Boost_BINARY_DIR}/libs/log/src/windows)
    endif()

    set(Boost_LIBRARIES "")  # cmake-lint: disable=C0103
    foreach(component ${BOOST_COMPONENTS})
        list(APPEND Boost_LIBRARIES "Boost::${component}")
    endforeach()
endif()

message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
