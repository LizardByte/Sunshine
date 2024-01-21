# the name of the target operating system
SET(CMAKE_SYSTEM_NAME Linux)

# set processor type
SET(CMAKE_SYSTEM_PROCESSOR x86_64)

SET(COMPILER_PREFIX ${CMAKE_SYSTEM_PROCESSOR}-linux-gnu)

# which compilers to use for C and C++
SET(CMAKE_ASM_COMPILER ${COMPILER_PREFIX}-gcc)
SET(CMAKE_ASM-ATT_COMPILER ${COMPILER_PREFIX}-gcc)
SET(CMAKE_C_COMPILER ${COMPILER_PREFIX}-gcc)
SET(CMAKE_CXX_COMPILER ${COMPILER_PREFIX}-g++)

# here is the target environment located
set(CMAKE_SYSROOT "/mnt/cross")

# adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_AR "${COMPILER_PREFIX}-gcc-ar" CACHE FILEPATH "Archive manager" FORCE)
set(CMAKE_RANLIB "${COMPILER_PREFIX}-gcc-ranlib" CACHE FILEPATH "Archive index generator" FORCE)

# packaging
set(CPACK_RPM_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
