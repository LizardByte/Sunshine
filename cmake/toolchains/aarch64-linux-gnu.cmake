# CMake toolchain file for cross-compiling to ARM64 (aarch64) on Debian/Ubuntu
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Use GCC version from environment variable if available
if(DEFINED ENV{LINUX_GCC_VERSION})  # cmake-lint: disable=W0106
    set(LINUX_GCC_VERSION "-$ENV{LINUX_GCC_VERSION}")
else()
    set(LINUX_GCC_VERSION "")  # default to no version suffix
endif()

# Set compiler prefix and target
set(CMAKE_C_COMPILER_TARGET ${CMAKE_SYSTEM_PROCESSOR}-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET ${CMAKE_C_COMPILER_TARGET})

# Which compilers to use for C and C++
set(CMAKE_C_COMPILER ${CMAKE_C_COMPILER_TARGET}-gcc${LINUX_GCC_VERSION})
set(CMAKE_CXX_COMPILER ${CMAKE_C_COMPILER_TARGET}-g++${LINUX_GCC_VERSION})
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER_TARGET}-gcc${LINUX_GCC_VERSION})

# Here is the target environment located
set(CMAKE_FIND_ROOT_PATH /usr/${CMAKE_C_COMPILER_TARGET})

# Packaging
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
