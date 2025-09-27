# CMake toolchain file for cross-compiling to x86_64 on Debian/Ubuntu
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-linux-gnu.cmake

# The name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)

# Set processor type
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Set compiler prefix
set(COMPILER_PREFIX ${CMAKE_SYSTEM_PROCESSOR}-linux-gnu)

# Which compilers to use for C and C++
set(CMAKE_C_COMPILER ${COMPILER_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${COMPILER_PREFIX}-g++)
set(CMAKE_ASM_COMPILER ${COMPILER_PREFIX}-gcc)

# Here is the target environment located
set(CMAKE_FIND_ROOT_PATH /usr/${COMPILER_PREFIX})

# Adjust the default behavior of the FIND_XXX() commands:
# search headers and libraries in the target environment, search
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Set pkg-config environment for cross-compilation
set(ENV{PKG_CONFIG_PATH} "/usr/lib/${COMPILER_PREFIX}/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${COMPILER_PREFIX}/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/usr/${COMPILER_PREFIX}")

# Explicitly set OpenSSL paths for cross-compilation
set(OPENSSL_ROOT_DIR "/usr/${COMPILER_PREFIX}")
set(OPENSSL_INCLUDE_DIR "/usr/${COMPILER_PREFIX}/include")
set(OPENSSL_CRYPTO_LIBRARY "/usr/${COMPILER_PREFIX}/lib/libcrypto.so")
set(OPENSSL_SSL_LIBRARY "/usr/${COMPILER_PREFIX}/lib/libssl.so")

# Additional library paths
set(CMAKE_LIBRARY_PATH "/usr/${COMPILER_PREFIX}/lib" ${CMAKE_LIBRARY_PATH})
set(CMAKE_INCLUDE_PATH "/usr/${COMPILER_PREFIX}/include" ${CMAKE_INCLUDE_PATH})

# Packaging
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
