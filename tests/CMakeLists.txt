cmake_minimum_required(VERSION 3.13)
# https://github.com/google/oss-policies-info/blob/main/foundational-cxx-support-matrix.md#foundational-c-support

project(test_sunshine)

set(PYTHON_PREFERRED_VERSION 3.11)
set(PYTHON_MINIMUM_VERSION 3.9)

include_directories("${CMAKE_SOURCE_DIR}")

enable_testing()

# Add GoogleTest directory to the project
set(GTEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third-party/googletest")
set(INSTALL_GTEST OFF)
set(INSTALL_GMOCK OFF)
add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest")
include_directories("${GTEST_SOURCE_DIR}/googletest/include" "${GTEST_SOURCE_DIR}")

# coverage
# https://gcovr.com/en/stable/guide/compiling.html#compiler-options
set(CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -O1")
set(CMAKE_C_FLAGS "-fprofile-arcs -ftest-coverage -O1")

# if windows
if (WIN32)
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # cmake-lint: disable=C0103
endif ()

# modify SUNSHINE_DEFINITIONS
if (WIN32)
    list(APPEND
            SUNSHINE_DEFINITIONS SUNSHINE_SHADERS_DIR="${CMAKE_SOURCE_DIR}/src_assets/windows/assets/shaders/directx")
elseif (NOT APPLE)
    list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_SHADERS_DIR="${CMAKE_SOURCE_DIR}/src_assets/linux/assets/shaders/opengl")
endif ()

set(TEST_DEFINITIONS)  # list will be appended as needed

# IF option TESTS_ENABLE_PYTHON_TESTS is ON, then we need to find python
if (TESTS_ENABLE_PYTHON_TESTS)
    if (NOT DEFINED TESTS_PYTHON_EXECUTABLE)
        # python is required for doc tests

        # https://github.com/actions/setup-python/issues/121#issuecomment-777748504
        if (POLICY CMP0094)  # https://cmake.org/cmake/help/latest/policy/CMP0094.html
            cmake_policy(SET CMP0094 NEW)  # FindPython should return the first matching Python
        endif ()

        # needed on GitHub Actions CI: actions/setup-python does not touch registry/frameworks on Windows/macOS
        # this mirrors PythonInterp behavior which did not consult registry/frameworks first
        if (NOT DEFINED Python_FIND_REGISTRY)
            set(Python_FIND_REGISTRY "LAST")  # cmake-lint: disable=C0103
        endif ()
        if (NOT DEFINED Python_FIND_FRAMEWORK)
            set(Python_FIND_FRAMEWORK "LAST")  # cmake-lint: disable=C0103
        endif ()

        # first, try to find preferred version of python
        find_package(Python ${PYTHON_PREFERRED_VERSION} EXACT COMPONENTS Interpreter)
        if (Python_FOUND)
            message(STATUS
                    "Preferred Python ${PYTHON_PREFERRED_VERSION} found, tests dependent on Python will be enabled")
        else ()
            # fallback to minimum version
            find_package(Python ${PYTHON_MINIMUM_VERSION} COMPONENTS Interpreter)
        endif ()
        if (Python_FOUND)
            message(STATUS "Python found, tests dependent on Python will be enabled")
            list(APPEND TEST_DEFINITIONS TESTS_ENABLE_VENV_TESTS=1)
            list(APPEND TEST_DEFINITIONS TESTS_PYTHON_EXECUTABLE="${Python_EXECUTABLE}")
        else ()
            message(SEND_ERROR "Python not found, tests dependent on Python will be disabled")
            list(APPEND TEST_DEFINITIONS TESTS_ENABLE_VENV_TESTS=0)
        endif ()
    else()
        message(STATUS "Python executable is set to ${TESTS_PYTHON_EXECUTABLE}")
        list(APPEND TEST_DEFINITIONS TESTS_ENABLE_VENV_TESTS=1)
        list(APPEND TEST_DEFINITIONS TESTS_PYTHON_EXECUTABLE="${TESTS_PYTHON_EXECUTABLE}")
    endif()
else ()
    message(STATUS "Python tests are disabled by 'TESTS_ENABLE_PYTHON_TESTS' option")
    list(APPEND TEST_DEFINITIONS TESTS_ENABLE_VENV_TESTS=0)
endif ()

list(APPEND TEST_DEFINITIONS TESTS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")  # add source directory to TEST_DEFINITIONS
list(APPEND TEST_DEFINITIONS TESTS_DOCS_DIR="${CMAKE_SOURCE_DIR}/docs")  # add docs directory to TEST_DEFINITIONS

# make sure TESTS_SOFTWARE_ENCODER_UNAVAILABLE is set to "fail" or "skip"
if (NOT (TESTS_SOFTWARE_ENCODER_UNAVAILABLE STREQUAL "fail" OR TESTS_SOFTWARE_ENCODER_UNAVAILABLE STREQUAL "skip"))
    set(TESTS_SOFTWARE_ENCODER_UNAVAILABLE "fail")
endif ()
list(APPEND TEST_DEFINITIONS TESTS_SOFTWARE_ENCODER_UNAVAILABLE="${TESTS_SOFTWARE_ENCODER_UNAVAILABLE}")  # fail/skip

file(GLOB_RECURSE TEST_SOURCES
        ${CMAKE_SOURCE_DIR}/tests/conftest.cpp
        ${CMAKE_SOURCE_DIR}/tests/utils.cpp
        ${CMAKE_SOURCE_DIR}/tests/test_*.cpp)

set(SUNSHINE_SOURCES
        ${SUNSHINE_TARGET_FILES})

# remove main.cpp from the list of sources
list(REMOVE_ITEM SUNSHINE_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_executable(${PROJECT_NAME}
        ${TEST_SOURCES}
        ${SUNSHINE_SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_link_libraries(${PROJECT_NAME}
        ${SUNSHINE_EXTERNAL_LIBRARIES}
        gtest
        gtest_main  # if we use this we don't need our own main function
        ${PLATFORM_LIBRARIES})
target_compile_definitions(${PROJECT_NAME} PUBLIC ${SUNSHINE_DEFINITIONS} ${TEST_DEFINITIONS})
target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SUNSHINE_COMPILE_OPTIONS}>;$<$<COMPILE_LANGUAGE:CUDA>:${SUNSHINE_COMPILE_OPTIONS_CUDA};-std=c++17>)  # cmake-lint: disable=C0301
target_link_options(${PROJECT_NAME} PRIVATE)

add_test(NAME ${PROJECT_NAME} COMMAND sunshine_test)
