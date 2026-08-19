#
# Loads the miniupnpc library giving priority to the system package first (unless static requested), with a fallback to FetchContent.
#
include_guard(GLOBAL)

option(SUNSHINE_MINIUPNPC_STATIC "Statically link miniupnpc" OFF)

if(NOT SUNSHINE_MINIUPNPC_STATIC)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(MINIUPNP QUIET miniupnpc)
    endif()
endif()

if(MINIUPNP_FOUND AND NOT SUNSHINE_MINIUPNPC_STATIC)
    message(STATUS "Found system miniupnpc: ${MINIUPNP_LIBRARIES}")
    include_directories(SYSTEM ${MINIUPNP_INCLUDE_DIRS})
else()
    message(STATUS "System miniupnpc not found or static requested. Falling back to FetchContent.")
    include(FetchContent)

    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
        cmake_policy(SET CMP0135 NEW)
    endif()

    set(UPNPC_BUILD_SHARED OFF CACHE BOOL "Build shared miniupnpc library" FORCE)
    set(UPNPC_BUILD_STATIC ON CACHE BOOL "Build static miniupnpc library" FORCE)
    set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Build miniupnpc tests" FORCE)
    set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Build miniupnpc sample" FORCE)

    FetchContent_Declare(
        miniupnpc
        GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
        GIT_TAG        miniupnpc_2_3_3
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  miniupnpc
    )
    FetchContent_MakeAvailable(miniupnpc)

    if(TARGET libminiupnpc-static)
        set(MINIUPNP_LIBRARIES libminiupnpc-static)
    elseif(TARGET miniupnpc-static)
        set(MINIUPNP_LIBRARIES miniupnpc-static)
    endif()

    # Create include directory structure for <miniupnpc/miniupnpc.h>
    set(MINIUPNPC_HEADER_DIR "${CMAKE_BINARY_DIR}/include/miniupnpc")
    file(MAKE_DIRECTORY "${MINIUPNPC_HEADER_DIR}")
    file(GLOB MINIUPNPC_HDR_FILES "${miniupnpc_SOURCE_DIR}/miniupnpc/include/*.h")
    foreach(hdr ${MINIUPNPC_HDR_FILES})
        get_filename_component(hdr_name "${hdr}" NAME)
        configure_file("${hdr}" "${MINIUPNPC_HEADER_DIR}/${hdr_name}" COPYONLY)
    endforeach()
    if(EXISTS "${miniupnpc_BINARY_DIR}/miniupnpcstrings.h")
        configure_file("${miniupnpc_BINARY_DIR}/miniupnpcstrings.h" "${MINIUPNPC_HEADER_DIR}/miniupnpcstrings.h" COPYONLY)
    endif()

    set(MINIUPNP_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/include" "${miniupnpc_SOURCE_DIR}/miniupnpc/include" "${miniupnpc_BINARY_DIR}")
    include_directories(SYSTEM ${MINIUPNP_INCLUDE_DIRS})
endif()
