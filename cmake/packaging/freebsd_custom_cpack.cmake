# FreeBSD post-build script to fix +POST_INSTALL and +PRE_DEINSTALL scripts
# in the generated .pkg file.
#
# This script runs AFTER CPack creates the .pkg file. We need to:
# 1. Extract the .pkg file (which is a tar.xz archive)
# 2. Add our install/deinstall scripts to the root
# 3. Remove script entries from the +MANIFEST files section
# 4. Repack the .pkg file using pkg-static

if(NOT CPACK_GENERATOR STREQUAL "FREEBSD")
    return()
endif()

message(STATUS "FreeBSD post-build: Processing install/deinstall scripts")

# Get script paths from the list we set
if(NOT DEFINED CPACK_FREEBSD_PACKAGE_SCRIPTS)
    message(FATAL_ERROR "FreeBSD post-build: CPACK_FREEBSD_PACKAGE_SCRIPTS not defined")
endif()

list(LENGTH CPACK_FREEBSD_PACKAGE_SCRIPTS _script_count)
if(_script_count EQUAL 0)
    message(FATAL_ERROR "FreeBSD post-build: CPACK_FREEBSD_PACKAGE_SCRIPTS is empty")
endif()

# Find the package file in CPACK_TOPLEVEL_DIRECTORY
file(GLOB _pkg_files "${CPACK_TOPLEVEL_DIRECTORY}/*.pkg")

if(NOT _pkg_files)
    message(FATAL_ERROR "FreeBSD post-build: No .pkg file found in ${CPACK_TOPLEVEL_DIRECTORY}")
endif()

list(GET _pkg_files 0 _pkg_file)
message(STATUS "FreeBSD post-build: Found package: ${_pkg_file}")

# Create a temporary directory for extraction
get_filename_component(_pkg_dir "${_pkg_file}" DIRECTORY)
set(_tmp_dir "${_pkg_dir}/pkg_repack_tmp")
file(REMOVE_RECURSE "${_tmp_dir}")
file(MAKE_DIRECTORY "${_tmp_dir}")

# Extract the package using tar (pkg files are tar.xz archives)
message(STATUS "FreeBSD post-build: Extracting package...")
find_program(TAR_EXECUTABLE tar REQUIRED)
find_program(PKG_STATIC_EXECUTABLE pkg-static REQUIRED)

execute_process(
    COMMAND ${TAR_EXECUTABLE} -xf ${_pkg_file} --no-same-owner --numeric-owner
    WORKING_DIRECTORY "${_tmp_dir}"
    RESULT_VARIABLE _extract_result
    ERROR_VARIABLE _extract_error
)

if(NOT _extract_result EQUAL 0)
    message(FATAL_ERROR "FreeBSD post-build: Failed to extract package: ${_extract_error}")
endif()

# Debug: Check what was extracted
file(GLOB_RECURSE _extracted_files RELATIVE "${_tmp_dir}" "${_tmp_dir}/*")
list(LENGTH _extracted_files _file_count)
message(STATUS "FreeBSD post-build: Extracted ${_file_count} files")

# Copy the install/deinstall scripts to the extracted package root
message(STATUS "FreeBSD post-build: Adding install/deinstall scripts...")

foreach(script_path ${CPACK_FREEBSD_PACKAGE_SCRIPTS})
    if(EXISTS "${script_path}")
        get_filename_component(_script_name "${script_path}" NAME)
        file(COPY "${script_path}"
             DESTINATION "${_tmp_dir}/"
             FILE_PERMISSIONS
             OWNER_READ OWNER_WRITE OWNER_EXECUTE
             GROUP_READ GROUP_EXECUTE
             WORLD_READ WORLD_EXECUTE)
        message(STATUS "  Added: ${_script_name}")
    else()
        message(FATAL_ERROR "FreeBSD post-build: Script not found: ${script_path}")
    endif()
endforeach()

# Repack the package using pkg-static create
message(STATUS "FreeBSD post-build: Repacking package...")

# Debug: Verify files before repacking
file(GLOB_RECURSE _files_before_repack RELATIVE "${_tmp_dir}" "${_tmp_dir}/*")
list(LENGTH _files_before_repack _count_before_repack)
message(STATUS "FreeBSD post-build: About to repack ${_count_before_repack} files")

# Debug: Check directory structure
if(EXISTS "${_tmp_dir}/usr")
    message(STATUS "FreeBSD post-build: Found usr directory in extracted package")
    file(GLOB_RECURSE _usr_files RELATIVE "${_tmp_dir}/usr" "${_tmp_dir}/usr/*")
    list(LENGTH _usr_files _usr_file_count)
    message(STATUS "FreeBSD post-build: usr directory contains ${_usr_file_count} files")
endif()

# Create metadata directory separate from rootdir
set(_metadata_dir "${_tmp_dir}/metadata")
file(MAKE_DIRECTORY "${_metadata_dir}")

# Move manifest and scripts to metadata directory
file(GLOB _metadata_files "${_tmp_dir}/+*")
foreach(meta_file ${_metadata_files})
    get_filename_component(_meta_name "${meta_file}" NAME)
    file(RENAME "${meta_file}" "${_metadata_dir}/${_meta_name}")
    message(STATUS "FreeBSD post-build: Moved ${_meta_name} to metadata directory")
endforeach()

# Use pkg-static create to rebuild the package
# pkg create -r rootdir -m manifestdir -o outdir
# The rootdir should contain the actual files (usr/local/...)
# The manifestdir should contain +MANIFEST and install scripts
execute_process(
    COMMAND ${PKG_STATIC_EXECUTABLE} create -r ${_tmp_dir} -m ${_metadata_dir} -o ${_pkg_dir}
    RESULT_VARIABLE _pack_result
    OUTPUT_VARIABLE _pack_output
    ERROR_VARIABLE _pack_error
)

if(NOT _pack_result EQUAL 0)
    message(FATAL_ERROR "FreeBSD post-build: Failed to repack package: ${_pack_error}")
endif()

# Find the generated package file (pkg create generates its own name based on manifest)
file(GLOB _new_pkg_files "${_pkg_dir}/Sunshine-*.pkg")
if(NOT _new_pkg_files)
    message(FATAL_ERROR "FreeBSD post-build: pkg-static create succeeded but no package file was generated")
endif()

list(GET _new_pkg_files 0 _generated_pkg)

# Replace the original package with the newly created one
file(REMOVE "${_pkg_file}")
file(RENAME "${_generated_pkg}" "${_pkg_file}")
message(STATUS "FreeBSD post-build: Successfully processed package")

# Clean up
file(REMOVE_RECURSE "${_tmp_dir}")
