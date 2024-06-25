cmake_minimum_required(VERSION 3.19)

# find doxygen and graphviz
find_package(Doxygen
        REQUIRED dot)

if(POLICY CMP0094)  # https://cmake.org/cmake/help/latest/policy/CMP0094.html
    cmake_policy(SET CMP0094 NEW)  # FindPython should return the first matching Python
endif()

# needed on GitHub Actions CI: actions/setup-python does not touch registry/frameworks on Windows/macOS
# this mirrors PythonInterp behavior which did not consult registry/frameworks first
if(NOT DEFINED Python_FIND_REGISTRY)
    set(Python_FIND_REGISTRY "LAST")  # cmake-lint: disable=C0103
endif()
if(NOT DEFINED Python_FIND_FRAMEWORK)
    set(Python_FIND_FRAMEWORK "LAST")  # cmake-lint: disable=C0103
endif()

# find python
if(NOT DEFINED Python_EXECUTABLE)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
else()
    set(Python3_EXECUTABLE "${Python_EXECUTABLE}")  # cmake-lint: disable=C0103
    message(STATUS "Using Python3_EXECUTABLE: ${Python3_EXECUTABLE}")
endif()

# create venv in build dir
set(VENV_DIR "${CMAKE_BINARY_DIR}/venv")

# create venv
execute_process(
    COMMAND ${Python3_EXECUTABLE} -m venv ${VENV_DIR}
    COMMAND_ERROR_IS_FATAL LAST
    RESULT_VARIABLE VENV_RESULT
)

unset(Python3_EXECUTABLE)

set(VENV_PATTERNS
        ${VENV_DIR}/bin/python
        ${VENV_DIR}/bin/python.exe
        ${VENV_DIR}/Scripts/python.exe
)

# set Python3_EXECUTABLE to the python interpreter in the venv
foreach(pattern ${VENV_PATTERNS})
    if(EXISTS ${pattern})
        set(Python3_EXECUTABLE "${pattern}")  # cmake-lint: disable=C0103
        get_filename_component(Python3_ROOT_DIR ${pattern} DIRECTORY)
        message(STATUS "Using Python3_EXECUTABLE (venv): ${Python3_EXECUTABLE}")
        message(STATUS "Using Python3_ROOT_DIR (venv): ${Python3_ROOT_DIR}")
        break()
    endif()
endforeach()

# fail if Python3_ROOT_DIR is not set
if(NOT DEFINED Python3_EXECUTABLE)
    message(FATAL_ERROR "Unable to setup python venv")
endif()

# call a simple python command
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "print('   Testing Python3_EXECUTABLE')"
    COMMAND_ERROR_IS_FATAL ANY
    RESULT_VARIABLE PYTHON_RESULT
)

# fail if the python command failed
if(NOT PYTHON_RESULT EQUAL 0)
    message(FATAL_ERROR "Python3_EXECUTABLE failed")
endif()

# install doc requirements
execute_process(
    COMMAND ${Python3_EXECUTABLE} -m pip install -r ${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt
    COMMAND_ERROR_IS_FATAL ANY
    RESULT_VARIABLE PIP_RESULT
)

# rst check
set(RST_PATTERNS
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/README.rst
)

# check rst files
foreach(pattern ${RST_PATTERNS})
    message(STATUS "Checking RST files: ${pattern}")
    execute_process(
        COMMAND "${Python3_ROOT_DIR}/rstcheck" -r "${pattern}"
        COMMAND_ERROR_IS_FATAL ANY
        RESULT_VARIABLE RST_RESULT
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()

# commands
list(APPEND SPHINX_BUILD_HTML_COMMAND
        "${Python3_ROOT_DIR}/sphinx-build"
        "-M"
        "html"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
        "${CMAKE_CURRENT_BINARY_DIR}/build/html"
        "-W"
        "--keep-going"
)

list(APPEND SPHINX_BUILD_EPUB_COMMAND
        "${Python3_ROOT_DIR}/sphinx-build"
        "-M"
        "epub"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
        "${CMAKE_CURRENT_BINARY_DIR}/build/epub"
        "-W"
        "--keep-going"
)

# build docs
add_custom_target(docs ALL
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Building documentation"
        COMMAND ${CMAKE_COMMAND} -E env DOXY_PATH=$<TARGET_FILE:Doxygen::doxygen> ${SPHINX_BUILD_HTML_COMMAND}
        COMMAND ${CMAKE_COMMAND} -E env DOXY_PATH=$<TARGET_FILE:Doxygen::doxygen> ${SPHINX_BUILD_EPUB_COMMAND}
        VERBATIM
)
