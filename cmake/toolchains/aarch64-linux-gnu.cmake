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
set(CMAKE_FIND_ROOT_PATH
    /usr/${CMAKE_C_COMPILER_TARGET}
    /usr/lib/${CMAKE_C_COMPILER_TARGET}
    /usr/include/${CMAKE_C_COMPILER_TARGET}
)

# adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# OpenSSL hints for cross-compilation
set(OPENSSL_ROOT_DIR /usr/${CMAKE_C_COMPILER_TARGET})
set(OPENSSL_INCLUDE_DIR /usr/include/${CMAKE_C_COMPILER_TARGET})
set(OPENSSL_CRYPTO_LIBRARY /usr/lib/${CMAKE_C_COMPILER_TARGET}/libcrypto.so)
set(OPENSSL_SSL_LIBRARY /usr/lib/${CMAKE_C_COMPILER_TARGET}/libssl.so)

# Configure pkg-config for cross-compilation
set(ENV{PKG_CONFIG_PATH} "/usr/lib/${CMAKE_C_COMPILER_TARGET}/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${CMAKE_C_COMPILER_TARGET}/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/")

# Use the cross-compilation pkg-config if available
find_program(PKG_CONFIG_EXECUTABLE NAMES ${CMAKE_C_COMPILER_TARGET}-pkg-config pkg-config)
if(PKG_CONFIG_EXECUTABLE)
    set(PKG_CONFIG_EXECUTABLE ${PKG_CONFIG_EXECUTABLE})
endif()

# Packaging
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
