#
# Generates the glad OpenGL/EGL loader library using the glad2 generator.
# Sources are generated at build time via the glad submodule's CMake integration.
#
include_guard(GLOBAL)

# glad's generator requires jinja2 at build time.  The Python interpreter must be
# discovered HERE — before add_subdirectory() — for two reasons:
#
#  1. glad's cmake/CMakeLists.txt calls find_package(PythonInterp) (the legacy
#     CMP0148 API, which reads the PYTHON_EXECUTABLE cache variable).  Whatever
#     interpreter is found there gets baked into every glad_add_library() build
#     rule.  If we discover Python only after add_subdirectory(), glad has already
#     committed to a different interpreter (e.g. Homebrew python@3.14 on PATH),
#     and our jinja2-equipped venv/system Python is never used.
#
#  2. Setting PYTHON_EXECUTABLE (legacy) = Python_EXECUTABLE (new-style) in the
#     cache before add_subdirectory() causes glad's find_package(PythonInterp) to
#     skip its own search and reuse our interpreter directly.
#
# GLAD_SKIP_PIP_INSTALL is a hard override for environments where pip cannot run
# at all (e.g. Flatpak, Homebrew). When OFF (the default) the code below checks
# whether jinja2+setuptools are importable and pip-installs them if they are not.
# When ON the caller is responsible for supplying a Python that already has jinja2,
# typically via -DPython_EXECUTABLE=/path/to/venv/python.
option(GLAD_SKIP_PIP_INSTALL
        "Hard-skip pip install of jinja2 even if it is not importable. \
Only needed in sandboxed build environments (e.g. Flatpak, Homebrew) where pip cannot run." OFF)

if(NOT GLAD_SKIP_PIP_INSTALL)
    # glad's generator requires Python >= 3.8 (importlib.metadata) and jinja2.
    # Prefer the real system Python over any venv/toolchain Python injected into PATH
    # (e.g. GitHub Actions setup-python). STANDARD means FindPython does not give
    # special priority to virtual environments.
    set(Python_FIND_VIRTUALENV STANDARD)  # cmake-lint: disable=C0103

    # On Linux/FreeBSD, search for a sufficiently new system Python (>= 3.8) explicitly.
    # This is important on distros like OpenSUSE Leap where /usr/bin/python3 is 3.6,
    # but python3.11 or python3.8 may also be installed. Search newest-first so that
    # the best available interpreter is used. The NO_DEFAULT_PATH on the first pass
    # restricts the search to /usr/bin and /usr/local/bin to prefer distro packages
    # over venv/toolchain Pythons (e.g. GitHub Actions setup-python injects its own
    # python3 first on PATH; Homebrew puts python@3.x in /home/linuxbrew/... on PATH).
    if(UNIX AND NOT APPLE)
        foreach(py_candidate python3.14 python3.13 python3.12 python3.11 python3.10 python3.9 python3.8 python3)
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
endif()

# Run find_package(Python) before add_subdirectory() so Python_EXECUTABLE is
# committed to the cache.  When GLAD_SKIP_PIP_INSTALL=OFF the system-Python search
# above has already set it; when GLAD_SKIP_PIP_INSTALL=ON the caller's
# -DPython_EXECUTABLE cache entry is honoured directly.
#
# Exception: when GLAD_SKIP_PIP_INSTALL=ON and Python_EXECUTABLE already points to
# an existing file (e.g. a Homebrew venv), skip find_package() entirely.
# find_package(Python) ignores the cache hint on some CMake/platform combinations
# and searches PATH instead, finding a different interpreter (e.g. Homebrew's own
# python@3.x).  Using the cache value directly is safe here because the caller has
# explicitly told us which interpreter to use.
if(GLAD_SKIP_PIP_INSTALL AND EXISTS "${Python_EXECUTABLE}")
    message(STATUS "glad: using provided Python interpreter: ${Python_EXECUTABLE}")
else()
    find_package(Python COMPONENTS Interpreter REQUIRED)
endif()

# Propagate to the legacy PYTHON_EXECUTABLE variable consumed by FindPythonInterp,
# which is what glad's cmake/CMakeLists.txt calls.  Doing this before
# add_subdirectory() ensures glad's internal find_package(PythonInterp) reuses our
# interpreter instead of doing its own PATH search.
set(PYTHON_EXECUTABLE "${Python_EXECUTABLE}" CACHE FILEPATH "Python interpreter for glad" FORCE)

# The glad2 repo does not have a root-level CMakeLists.txt; its CMake integration
# lives in cmake/CMakeLists.txt which provides the glad_add_library() function.
#
# glad 2.0.0's cmake/CMakeLists.txt calls cmake_minimum_required with a version < 3.5.
# CMake >= 3.27 turned this into a hard error. Setting CMAKE_POLICY_VERSION_MINIMUM
# to 3.5 allows the subdirectory to configure without error.
# We unset it immediately afterwards so it does not affect anything else.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/glad/cmake" glad2-cmake)
unset(CMAKE_POLICY_VERSION_MINIMUM)

if(NOT GLAD_SKIP_PIP_INSTALL)
    # Check whether jinja2 and pkg_resources (setuptools) are already importable.
    # pkg_resources is provided by setuptools, which is no longer bundled with Python 3.12+
    # on several distros (Debian Trixie, Arch Linux, COPR, FreeBSD, etc.).
    execute_process(
            COMMAND "${Python_EXECUTABLE}" -c "import jinja2; import pkg_resources"
            RESULT_VARIABLE _glad_deps_import_result
            OUTPUT_QUIET
            ERROR_QUIET
    )

    if(NOT _glad_deps_import_result EQUAL 0)
        message(STATUS "glad: jinja2 or setuptools not found in ${Python_EXECUTABLE}, installing via pip...")

        # Some system Python installations (e.g. FreeBSD ports) ship without pip.
        # Try to bootstrap it via ensurepip before falling back to the pip install.
        execute_process(
                COMMAND "${Python_EXECUTABLE}" -m pip --version
                RESULT_VARIABLE _pip_available
                OUTPUT_QUIET ERROR_QUIET
        )
        if(NOT _pip_available EQUAL 0)
            message(STATUS "glad: pip not found in ${Python_EXECUTABLE}, bootstrapping via ensurepip...")
            execute_process(
                    COMMAND "${Python_EXECUTABLE}" -m ensurepip --upgrade
                    RESULT_VARIABLE _ensurepip_result
                    OUTPUT_QUIET ERROR_QUIET
            )
            if(NOT _ensurepip_result EQUAL 0)
                message(FATAL_ERROR
                        "glad: pip is not available in ${Python_EXECUTABLE} and ensurepip failed to "
                        "bootstrap it.\nPlease install pip for your Python interpreter "
                        "(e.g. 'pkg install py311-pip' on FreeBSD, or the python3-pip package for "
                        "your distro) and re-run cmake.")
            endif()
        endif()

        execute_process(
                COMMAND "${Python_EXECUTABLE}" -m pip install
                    --upgrade
                    --requirement "${CMAKE_SOURCE_DIR}/third-party/glad/requirements.txt"
                    "setuptools<81"
                    --quiet
                COMMAND_ERROR_IS_FATAL ANY
        )
    else()
        message(STATUS "glad: jinja2 and setuptools already available in ${Python_EXECUTABLE}, skipping pip install")
    endif()
endif()

# Generate the glad libraries.
# REPRODUCIBLE avoids fetching the latest spec XML from Khronos at generation time.
#
# NOTE: glad v2.0.0's glad_add_library() does not deduplicate OUTPUT files when multiple APIs
# share a common header (e.g. KHR/khrplatform.h is emitted by both the "egl" and "gl" specs).
# Passing both APIs in a single call causes ninja to error with "multiple rules generate
# gladsources/glad/include/KHR/khrplatform.h". Work around this by using one library per API
# and combining them into a single INTERFACE target named "glad" for the rest of the project.

# EGL 1.5 --loader --mx
# EGL_EXT_image_dma_buf_import_modifiers is included to expose eglQueryDmaBufFormatsEXT and
# eglQueryDmaBufModifiersEXT
glad_add_library(glad_egl
        STATIC
        REPRODUCIBLE
        LOCATION "${CMAKE_BINARY_DIR}/gladsources/glad_egl"
        LOADER
        MX
        API
            "egl=1.5"
        EXTENSIONS
            EGL_EXT_image_dma_buf_import
            EGL_EXT_image_dma_buf_import_modifiers
            EGL_EXT_platform_base
            EGL_EXT_platform_wayland
            EGL_EXT_platform_x11
            EGL_IMG_context_priority
            EGL_KHR_create_context
            EGL_KHR_image_base
            EGL_KHR_surfaceless_context
            EGL_MESA_platform_gbm
)

# GL compatibility=4.6 --loader --mx
glad_add_library(glad_gl
        STATIC
        REPRODUCIBLE
        LOCATION "${CMAKE_BINARY_DIR}/gladsources/glad_gl"
        LOADER
        MX
        API
            "gl:compatibility=4.6"
)

# Combine both into a single INTERFACE target so the rest of the project can simply link "glad".
add_library(glad INTERFACE)
target_link_libraries(glad INTERFACE glad_egl glad_gl)
