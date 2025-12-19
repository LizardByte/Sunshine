# CMake toolchain file for cross-compiling to Windows ARM64
# using llvm-mingw toolchain

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Toolchain paths - adjust TOOLCHAIN_ROOT as needed
if(NOT DEFINED TOOLCHAIN_ROOT)
    set(TOOLCHAIN_ROOT "$ENV{HOME}/toolchains/llvm-mingw")
endif()

set(TARGET_TRIPLE "aarch64-w64-mingw32")

# Compilers
set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-clang++")
set(CMAKE_RC_COMPILER "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-windres")
set(CMAKE_AR "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-ar")
set(CMAKE_RANLIB "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-ranlib")

# Sysroot
set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}/${TARGET_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}/${TARGET_TRIPLE}")

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Compiler flags
set(CMAKE_C_FLAGS_INIT "-D_WIN32_WINNT=0x0A00")
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++ -D_WIN32_WINNT=0x0A00")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -lc++ -lc++abi")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -lc++ -lc++abi")

# Windows-specific
set(WIN32 TRUE)
set(MINGW TRUE)
