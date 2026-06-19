# - Try to find Libcap
# Once done this will define
#
#  LIBCAP_FOUND - system has Libcap
#  LIBCAP_INCLUDE_DIRS - the Libcap include directory
#  LIBCAP_LIBRARIES - the libraries needed to use Libcap
#  LIBCAP_DEFINITIONS - Compiler switches required for using Libcap

# Use pkg-config to get the directories and then use these values
# in the find_path() and find_library() calls
find_package(PkgConfig)
pkg_check_modules(PC_LIBCAP libcap)

set(LIBCAP_DEFINITIONS ${PC_LIBCAP_CFLAGS})

find_path(LIBCAP_INCLUDE_DIRS sys/capability.h PATHS ${PC_LIBCAP_INCLUDEDIR} ${PC_LIBCAP_INCLUDE_DIRS})
find_library(LIBCAP_LIBRARIES NAMES libcap.so PATHS ${PC_LIBCAP_LIBDIR} ${PC_LIBCAP_LIBRARY_DIRS})
mark_as_advanced(LIBCAP_INCLUDE_DIRS LIBCAP_LIBRARIES)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBCAP REQUIRED_VARS LIBCAP_LIBRARIES LIBCAP_INCLUDE_DIRS)
