# common target definitions
# this file will also load platform specific macros

add_executable(sunshine ${SUNSHINE_TARGET_FILES})
set_target_properties(sunshine PROPERTIES CXX_STANDARD 17
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

# platform specific target definitions
if(WIN32)
    include(${CMAKE_MODULE_PATH}/targets/windows.cmake)
elseif(UNIX)
    include(${CMAKE_MODULE_PATH}/targets/unix.cmake)

    if(APPLE)
        include(${CMAKE_MODULE_PATH}/targets/macos.cmake)
    else()
        include(${CMAKE_MODULE_PATH}/targets/linux.cmake)
    endif()
endif()

# todo - is this necessary? ... for anything except linux?
if(NOT DEFINED CMAKE_CUDA_STANDARD)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

target_link_libraries(sunshine ${SUNSHINE_EXTERNAL_LIBRARIES} ${EXTRA_LIBS})
target_compile_definitions(sunshine PUBLIC ${SUNSHINE_DEFINITIONS})

# CLion complains about unknown flags after running cmake, and cannot add symbols to the index for cuda files
if(CUDA_INHERIT_COMPILE_OPTIONS)
    foreach(flag IN LISTS SUNSHINE_COMPILE_OPTIONS)
        list(APPEND SUNSHINE_COMPILE_OPTIONS_CUDA "$<$<COMPILE_LANGUAGE:CUDA>:--compiler-options=${flag}>")
    endforeach()
endif()

target_compile_options(sunshine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SUNSHINE_COMPILE_OPTIONS}>;$<$<COMPILE_LANGUAGE:CUDA>:${SUNSHINE_COMPILE_OPTIONS_CUDA};-std=c++17>)  # cmake-lint: disable=C0301

#WebUI build
add_custom_target(web-ui ALL
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Installing NPM Dependencies and Building the Web UI"
        COMMAND bash -c \"npm install && SUNSHINE_SOURCE_ASSETS_DIR=${SUNSHINE_SOURCE_ASSETS_DIR} SUNSHINE_ASSETS_DIR=${CMAKE_BINARY_DIR} npm run build\") # cmake-lint: disable=C0301
