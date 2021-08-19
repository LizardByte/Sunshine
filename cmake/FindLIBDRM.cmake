# - Try to find Libdrm
# Once done this will define
#
#  LIBDRM_FOUND - system has Libdrm
#  LIBDRM_INCLUDE_DIRS - the Libdrm include directory
#  LIBDRM_LIBRARIES - the libraries needed to use Libdrm
#  LIBDRM_DEFINITIONS - Compiler switches required for using Libdrm

# Use pkg-config to get the directories and then use these values
# in the find_path() and find_library() calls
find_package(PkgConfig)
pkg_check_modules(PC_LIBDRM libdrm)

set(LIBDRM_DEFINITIONS ${PC_LIBDRM_CFLAGS})

find_path(LIBDRM_INCLUDE_DIRS drm.h PATHS ${PC_LIBDRM_INCLUDEDIR} ${PC_LIBDRM_INCLUDE_DIRS} PATH_SUFFIXES libdrm)
find_library(LIBDRM_LIBRARIES NAMES libdrm.so PATHS ${PC_LIBDRM_LIBDIR} ${PC_LIBDRM_LIBRARY_DIRS})
mark_as_advanced(LIBDRM_INCLUDE_DIRS LIBDRM_LIBRARIES)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBDRM REQUIRED_VARS LIBDRM_LIBRARIES LIBDRM_INCLUDE_DIRS)