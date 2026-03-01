#
# Generates the glad OpenGL/EGL loader library using the glad2 generator.
# Sources are generated at build time via the glad submodule's CMake integration.
#
include_guard(GLOBAL)

# The glad2 repo does not have a root-level CMakeLists.txt; its CMake integration
# lives in cmake/CMakeLists.txt which provides the glad_add_library() function.
add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/glad/cmake" glad2-cmake)

# glad's generator requires jinja2. Find Python first (same interpreter that glad_add_library()
# will bake into the ninja build command), then check if jinja2 is already importable.
# Only pip-install if it isn't — this transparently handles system packages (python3-jinja2 on
# Debian/Ubuntu/Arch/COPR) as well as bare Python environments.
#
# GLAD_SKIP_PIP_INSTALL is a hard override for sandboxed environments where pip cannot run at
# all (e.g. Flatpak). In all normal cases leave it OFF and let the check below decide.
option(GLAD_SKIP_PIP_INSTALL
        "Hard-skip pip install of jinja2 even if it is not importable. \
Only needed in sandboxed build environments (e.g. Flatpak) where pip cannot run." OFF)

if(NOT GLAD_SKIP_PIP_INSTALL)
    # glad's generator requires Python >= 3.8 (importlib.metadata) and jinja2.
    # Prefer the real system Python over any venv/toolchain Python injected into PATH
    # (e.g. GitHub Actions setup-python). STANDARD means FindPython does not give
    # special priority to virtual environments.
    set(Python_FIND_VIRTUALENV STANDARD)  # cmake-lint: disable=C0103

    # On Linux, search for a sufficiently new system Python (>= 3.8) explicitly.
    # This is important on distros like OpenSUSE Leap where /usr/bin/python3 is 3.6,
    # but python3.11 or python3.8 may also be installed. Search newest-first so that
    # the best available interpreter is used. The NO_DEFAULT_PATH on the first pass
    # restricts the search to /usr/bin to prefer distro packages over venv/toolchain
    # Pythons (e.g. GitHub Actions setup-python injects its own python3 first on PATH).
    if(UNIX AND NOT APPLE)
        foreach(py_candidate python3.13 python3.12 python3.11 python3.10 python3.9 python3.8 python3)
            find_program(_system_python3 "${py_candidate}" PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
            if(_system_python3)
                # Verify this interpreter is >= 3.8
                execute_process(
                        COMMAND "${_system_python3}" -c
                            "import sys; sys.exit(0 if sys.version_info >= (3,8) else 1)"
                        RESULT_VARIABLE _py_version_ok
                        OUTPUT_QUIET ERROR_QUIET
                )
                if(_py_version_ok EQUAL 0)
                    message(STATUS "glad: using Python interpreter: ${_system_python3}")
                    set(Python_EXECUTABLE "${_system_python3}"  # cmake-lint: disable=C0103
                            CACHE FILEPATH "Python interpreter" FORCE)
                    break()
                else()
                    message(STATUS "glad: skipping ${_system_python3} (< 3.8)")
                    unset(_system_python3 CACHE)
                endif()
            endif()
            unset(_system_python3 CACHE)
        endforeach()
    endif()

    find_package(Python COMPONENTS Interpreter REQUIRED)

    # Check whether jinja2 is already importable by the found interpreter.
    execute_process(
            COMMAND "${Python_EXECUTABLE}" -c "import jinja2"
            RESULT_VARIABLE _jinja2_import_result
            OUTPUT_QUIET
            ERROR_QUIET
    )

    if(NOT _jinja2_import_result EQUAL 0)
        message(STATUS "glad: jinja2 not found in ${Python_EXECUTABLE}, installing via pip...")
        execute_process(
                COMMAND "${Python_EXECUTABLE}" -m pip install
                    --requirement "${CMAKE_SOURCE_DIR}/third-party/glad/requirements.txt"
                    --quiet
                COMMAND_ERROR_IS_FATAL ANY
        )
    else()
        message(STATUS "glad: jinja2 already available in ${Python_EXECUTABLE}, skipping pip install")
    endif()
endif()

# Generate the glad library.
#   EGL 1.5 --loader --mx
#   GL compatibility=4.6 --loader --mx
# EGL_EXT_image_dma_buf_import_modifiers is included to expose eglQueryDmaBufFormatsEXT and
# eglQueryDmaBufModifiersEXT
# REPRODUCIBLE avoids fetching the latest spec XML from Khronos at generation time.
glad_add_library(glad
        STATIC
        REPRODUCIBLE
        LOCATION "${CMAKE_BINARY_DIR}/gladsources/glad"
        LOADER
        MX
        API
            "egl=1.5"
            "gl:compatibility=4.6"
        EXTENSIONS
            EGL_EXT_image_dma_buf_import
            EGL_EXT_image_dma_buf_import_modifiers
)
