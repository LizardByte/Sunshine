# CPM.cmake - CMake's missing package manager
# ===========================================
# See https://github.com/cpm-cmake/CPM.cmake for usage and update instructions.
#
# MIT License
# -----------
#[[
  Copyright (c) 2019-2023 Lars Melchior and contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
]]

cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

# Initialize logging prefix
if(NOT CPM_INDENT)
  set(CPM_INDENT
      "CPM:"
      CACHE INTERNAL ""
  )
endif()

if(NOT COMMAND cpm_message)
  function(cpm_message)
    message(${ARGV})
  endfunction()
endif()

if(DEFINED EXTRACTED_CPM_VERSION)
  set(CURRENT_CPM_VERSION "${EXTRACTED_CPM_VERSION}${CPM_DEVELOPMENT}")
else()
  set(CURRENT_CPM_VERSION 0.42.1)
endif()

get_filename_component(CPM_CURRENT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
if(CPM_DIRECTORY)
  if(NOT CPM_DIRECTORY STREQUAL CPM_CURRENT_DIRECTORY)
    if(CPM_VERSION VERSION_LESS CURRENT_CPM_VERSION)
      message(
        AUTHOR_WARNING
          "${CPM_INDENT} \
A dependency is using a more recent CPM version (${CURRENT_CPM_VERSION}) than the current project (${CPM_VERSION}). \
It is recommended to upgrade CPM to the most recent version. \
See https://github.com/cpm-cmake/CPM.cmake for more information."
      )
    endif()
    if(${CMAKE_VERSION} VERSION_LESS "3.17.0")
      include(FetchContent)
    endif()
    return()
  endif()

  get_property(
    CPM_INITIALIZED GLOBAL ""
    PROPERTY CPM_INITIALIZED
    SET
  )
  if(CPM_INITIALIZED)
    return()
  endif()
endif()

if(CURRENT_CPM_VERSION MATCHES "development-version")
  message(
    WARNING "${CPM_INDENT} Your project is using an unstable development version of CPM.cmake. \
Please update to a recent release if possible. \
See https://github.com/cpm-cmake/CPM.cmake for details."
  )
endif()

set_property(GLOBAL PROPERTY CPM_INITIALIZED true)

macro(cpm_set_policies)
  # the policy allows us to change options without caching
  cmake_policy(SET CMP0077 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

  # the policy allows us to change set(CACHE) without caching
  if(POLICY CMP0126)
    cmake_policy(SET CMP0126 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
  endif()

  # The policy uses the download time for timestamp, instead of the timestamp in the archive. This
  # allows for proper rebuilds when a projects url changes
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)
  endif()

  # treat relative git repository paths as being relative to the parent project's remote
  if(POLICY CMP0150)
    cmake_policy(SET CMP0150 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0150 NEW)
  endif()
endmacro()
cpm_set_policies()

option(CPM_USE_LOCAL_PACKAGES "Always try to use `find_package` to get dependencies"
       $ENV{CPM_USE_LOCAL_PACKAGES}
)
option(CPM_LOCAL_PACKAGES_ONLY "Only use `find_package` to get dependencies"
       $ENV{CPM_LOCAL_PACKAGES_ONLY}
)
option(CPM_DOWNLOAD_ALL "Always download dependencies from source" $ENV{CPM_DOWNLOAD_ALL})
option(CPM_DONT_UPDATE_MODULE_PATH "Don't update the module path to allow using find_package"
       $ENV{CPM_DONT_UPDATE_MODULE_PATH}
)
option(CPM_DONT_CREATE_PACKAGE_LOCK "Don't create a package lock file in the binary path"
       $ENV{CPM_DONT_CREATE_PACKAGE_LOCK}
)
option(CPM_INCLUDE_ALL_IN_PACKAGE_LOCK
       "Add all packages added through CPM.cmake to the package lock"
       $ENV{CPM_INCLUDE_ALL_IN_PACKAGE_LOCK}
)
option(CPM_USE_NAMED_CACHE_DIRECTORIES
       "Use additional directory of package name in cache on the most nested level."
       $ENV{CPM_USE_NAMED_CACHE_DIRECTORIES}
)

set(CPM_VERSION
    ${CURRENT_CPM_VERSION}
    CACHE INTERNAL ""
)
set(CPM_DIRECTORY
    ${CPM_CURRENT_DIRECTORY}
    CACHE INTERNAL ""
)
set(CPM_FILE
    ${CMAKE_CURRENT_LIST_FILE}
    CACHE INTERNAL ""
)
set(CPM_PACKAGES
    ""
    CACHE INTERNAL ""
)
set(CPM_DRY_RUN
    OFF
    CACHE INTERNAL "Don't download or configure dependencies (for testing)"
)

if(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_SOURCE_CACHE_DEFAULT $ENV{CPM_SOURCE_CACHE})
else()
  set(CPM_SOURCE_CACHE_DEFAULT OFF)
endif()

set(CPM_SOURCE_CACHE
    ${CPM_SOURCE_CACHE_DEFAULT}
    CACHE PATH "Directory to download CPM dependencies"
)

if(NOT CPM_DONT_UPDATE_MODULE_PATH AND NOT DEFINED CMAKE_FIND_PACKAGE_REDIRECTS_DIR)
  set(CPM_MODULE_PATH
      "${CMAKE_BINARY_DIR}/CPM_modules"
      CACHE INTERNAL ""
  )
  # remove old modules
  file(REMOVE_RECURSE ${CPM_MODULE_PATH})
  file(MAKE_DIRECTORY ${CPM_MODULE_PATH})
  # locally added CPM modules should override global packages
  set(CMAKE_MODULE_PATH "${CPM_MODULE_PATH};${CMAKE_MODULE_PATH}")
endif()

if(NOT CPM_DONT_CREATE_PACKAGE_LOCK)
  set(CPM_PACKAGE_LOCK_FILE
      "${CMAKE_BINARY_DIR}/cpm-package-lock.cmake"
      CACHE INTERNAL ""
  )
  file(WRITE ${CPM_PACKAGE_LOCK_FILE}
       "# CPM Package Lock\n# This file should be committed to version control\n\n"
  )
endif()

include(FetchContent)

# Try to infer package name from git repository uri (path or url)
function(cpm_package_name_from_git_uri URI RESULT)
  if("${URI}" MATCHES "([^/:]+)/?.git/?$")
    set(${RESULT}
        ${CMAKE_MATCH_1}
        PARENT_SCOPE
    )
  else()
    unset(${RESULT} PARENT_SCOPE)
  endif()
endfunction()

# Find the shortest hash that can be used eg, if origin_hash is
# cccb77ae9609d2768ed80dd42cec54f77b1f1455 the following files will be checked, until one is found
# that is either empty (allowing us to assign origin_hash), or whose contents matches ${origin_hash}
#
# * .../cccb.hash
# * .../cccb77ae.hash
# * .../cccb77ae9609.hash
# * .../cccb77ae9609d276.hash
# * etc
#
# We will be able to use a shorter path with very high probability, but in the (rare) event that the
# first couple characters collide, we will check longer and longer substrings.
function(cpm_get_shortest_hash source_cache_dir origin_hash short_hash_output_var)
  # for compatibility with caches populated by a previous version of CPM, check if a directory using
  # the full hash already exists
  if(EXISTS "${source_cache_dir}/${origin_hash}")
    set(${short_hash_output_var}
        "${origin_hash}"
        PARENT_SCOPE
    )
    return()
  endif()

  foreach(len RANGE 4 40 4)
    string(SUBSTRING "${origin_hash}" 0 ${len} short_hash)
    set(hash_lock ${source_cache_dir}/${short_hash}.lock)
    set(hash_fp ${source_cache_dir}/${short_hash}.hash)
    # Take a lock, so we don't have a race condition with another instance of cmake. We will release
    # this lock when we can, however, if there is an error, we want to ensure it gets released on
    # it's own on exit from the function.
    file(LOCK ${hash_lock} GUARD FUNCTION)

    # Load the contents of .../${short_hash}.hash
    file(TOUCH ${hash_fp})
    file(READ ${hash_fp} hash_fp_contents)

    if(hash_fp_contents STREQUAL "")
      # Write the origin hash
      file(WRITE ${hash_fp} ${origin_hash})
      file(LOCK ${hash_lock} RELEASE)
      break()
    elseif(hash_fp_contents STREQUAL origin_hash)
      file(LOCK ${hash_lock} RELEASE)
      break()
    else()
      file(LOCK ${hash_lock} RELEASE)
    endif()
  endforeach()
  set(${short_hash_output_var}
      "${short_hash}"
      PARENT_SCOPE
  )
endfunction()

# Try to infer package name and version from a url
function(cpm_package_name_and_ver_from_url url outName outVer)
  if(url MATCHES "[/\\?]([a-zA-Z0-9_\\.-]+)\\.(tar|tar\\.gz|tar\\.bz2|zip|ZIP)(\\?|/|$)")
    # We matched an archive
    set(filename "${CMAKE_MATCH_1}")

    if(filename MATCHES "([a-zA-Z0-9_\\.-]+)[_-]v?(([0-9]+\\.)*[0-9]+[a-zA-Z0-9]*)")
      # We matched <name>-<version> (ie foo-1.2.3)
      set(${outName}
          "${CMAKE_MATCH_1}"
          PARENT_SCOPE
      )
      set(${outVer}
          "${CMAKE_MATCH_2}"
          PARENT_SCOPE
      )
    elseif(filename MATCHES "(([0-9]+\\.)+[0-9]+[a-zA-Z0-9]*)")
      # We couldn't find a name, but we found a version
      #
      # In many cases (which we don't handle here) the url would look something like
      # `irrelevant/ACTUAL_PACKAGE_NAME/irrelevant/1.2.3.zip`. In such a case we can't possibly
      # distinguish the package name from the irrelevant bits. Moreover if we try to match the
      # package name from the filename, we'd get bogus at best.
      unset(${outName} PARENT_SCOPE)
      set(${outVer}
          "${CMAKE_MATCH_1}"
          PARENT_SCOPE
      )
    else()
      # Boldly assume that the file name is the package name.
      #
      # Yes, something like `irrelevant/ACTUAL_NAME/irrelevant/download.zip` will ruin our day, but
      # such cases should be quite rare. No popular service does this... we think.
      set(${outName}
          "${filename}"
          PARENT_SCOPE
      )
      unset(${outVer} PARENT_SCOPE)
    endif()
  else()
    # No ideas yet what to do with non-archives
    unset(${outName} PARENT_SCOPE)
    unset(${outVer} PARENT_SCOPE)
  endif()
endfunction()

function(cpm_find_package NAME VERSION)
  string(REPLACE " " ";" EXTRA_ARGS "${ARGN}")
  find_package(${NAME} ${VERSION} ${EXTRA_ARGS} QUIET)
  if(${CPM_ARGS_NAME}_FOUND)
    if(DEFINED ${CPM_ARGS_NAME}_VERSION)
      set(VERSION ${${CPM_ARGS_NAME}_VERSION})
    endif()
    cpm_message(STATUS "${CPM_INDENT} Using local package ${CPM_ARGS_NAME}@${VERSION}")
    CPMRegisterPackage(${CPM_ARGS_NAME} "${VERSION}")
    set(CPM_PACKAGE_FOUND
        YES
        PARENT_SCOPE
    )
  else()
    set(CPM_PACKAGE_FOUND
        NO
        PARENT_SCOPE
    )
  endif()
endfunction()

# Create a custom FindXXX.cmake module for a CPM package This prevents `find_package(NAME)` from
# finding the system library
function(cpm_create_module_file Name)
  if(NOT CPM_DONT_UPDATE_MODULE_PATH)
    if(DEFINED CMAKE_FIND_PACKAGE_REDIRECTS_DIR)
      # Redirect find_package calls to the CPM package. This is what FetchContent does when you set
      # OVERRIDE_FIND_PACKAGE. The CMAKE_FIND_PACKAGE_REDIRECTS_DIR works for find_package in CONFIG
      # mode, unlike the Find${Name}.cmake fallback. CMAKE_FIND_PACKAGE_REDIRECTS_DIR is not defined
      # in script mode, or in CMake < 3.24.
      # https://cmake.org/cmake/help/latest/module/FetchContent.html#fetchcontent-find-package-integration-examples
      string(TOLOWER ${Name} NameLower)
      file(WRITE ${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/${NameLower}-config.cmake
           "include(\"\${CMAKE_CURRENT_LIST_DIR}/${NameLower}-extra.cmake\" OPTIONAL)\n"
           "include(\"\${CMAKE_CURRENT_LIST_DIR}/${Name}Extra.cmake\" OPTIONAL)\n"
      )
      file(WRITE ${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/${NameLower}-config-version.cmake
           "set(PACKAGE_VERSION_COMPATIBLE TRUE)\n" "set(PACKAGE_VERSION_EXACT TRUE)\n"
      )
    else()
      file(WRITE ${CPM_MODULE_PATH}/Find${Name}.cmake
           "include(\"${CPM_FILE}\")\n${ARGN}\nset(${Name}_FOUND TRUE)"
      )
    endif()
  endif()
endfunction()

# Find a package locally or fallback to CPMAddPackage
function(CPMFindPackage)
  set(oneValueArgs NAME VERSION GIT_TAG FIND_PACKAGE_ARGUMENTS)

  cmake_parse_arguments(CPM_ARGS "" "${oneValueArgs}" "" ${ARGN})

  if(NOT DEFINED CPM_ARGS_VERSION)
    if(DEFINED CPM_ARGS_GIT_TAG)
      cpm_get_version_from_git_tag("${CPM_ARGS_GIT_TAG}" CPM_ARGS_VERSION)
    endif()
  endif()

  set(downloadPackage ${CPM_DOWNLOAD_ALL})
  if(DEFINED CPM_DOWNLOAD_${CPM_ARGS_NAME})
    set(downloadPackage ${CPM_DOWNLOAD_${CPM_ARGS_NAME}})
  elseif(DEFINED ENV{CPM_DOWNLOAD_${CPM_ARGS_NAME}})
    set(downloadPackage $ENV{CPM_DOWNLOAD_${CPM_ARGS_NAME}})
  endif()
  if(downloadPackage)
    CPMAddPackage(${ARGN})
    cpm_export_variables(${CPM_ARGS_NAME})
    return()
  endif()

  cpm_find_package(${CPM_ARGS_NAME} "${CPM_ARGS_VERSION}" ${CPM_ARGS_FIND_PACKAGE_ARGUMENTS})

  if(NOT CPM_PACKAGE_FOUND)
    CPMAddPackage(${ARGN})
    cpm_export_variables(${CPM_ARGS_NAME})
  endif()

endfunction()

# checks if a package has been added before
function(cpm_check_if_package_already_added CPM_ARGS_NAME CPM_ARGS_VERSION)
  if("${CPM_ARGS_NAME}" IN_LIST CPM_PACKAGES)
    CPMGetPackageVersion(${CPM_ARGS_NAME} CPM_PACKAGE_VERSION)
    if("${CPM_PACKAGE_VERSION}" VERSION_LESS "${CPM_ARGS_VERSION}")
      message(
        WARNING
          "${CPM_INDENT} Requires a newer version of ${CPM_ARGS_NAME} (${CPM_ARGS_VERSION}) than currently included (${CPM_PACKAGE_VERSION})."
      )
    endif()
    cpm_get_fetch_properties(${CPM_ARGS_NAME})
    set(${CPM_ARGS_NAME}_ADDED NO)
    set(CPM_PACKAGE_ALREADY_ADDED
        YES
        PARENT_SCOPE
    )
    cpm_export_variables(${CPM_ARGS_NAME})
  else()
    set(CPM_PACKAGE_ALREADY_ADDED
        NO
        PARENT_SCOPE
    )
  endif()
endfunction()

# Parse the argument of CPMAddPackage in case a single one was provided and convert it to a list of
# arguments which can then be parsed idiomatically. For example gh:foo/bar@1.2.3 will be converted
# to: GITHUB_REPOSITORY;foo/bar;VERSION;1.2.3
function(cpm_parse_add_package_single_arg arg outArgs)
  # Look for a scheme
  if("${arg}" MATCHES "^([a-zA-Z]+):(.+)$")
    string(TOLOWER "${CMAKE_MATCH_1}" scheme)
    set(uri "${CMAKE_MATCH_2}")

    # Check for CPM-specific schemes
    if(scheme STREQUAL "gh")
      set(out "GITHUB_REPOSITORY;${uri}")
      set(packageType "git")
    elseif(scheme STREQUAL "gl")
      set(out "GITLAB_REPOSITORY;${uri}")
      set(packageType "git")
    elseif(scheme STREQUAL "bb")
      set(out "BITBUCKET_REPOSITORY;${uri}")
      set(packageType "git")
      # A CPM-specific scheme was not found. Looks like this is a generic URL so try to determine
      # type
    elseif(arg MATCHES ".git/?(@|#|$)")
      set(out "GIT_REPOSITORY;${arg}")
      set(packageType "git")
    else()
      # Fall back to a URL
      set(out "URL;${arg}")
      set(packageType "archive")

      # We could also check for SVN since FetchContent supports it, but SVN is so rare these days.
      # We just won't bother with the additional complexity it will induce in this function. SVN is
      # done by multi-arg
    endif()
  else()
    if(arg MATCHES ".git/?(@|#|$)")
      set(out "GIT_REPOSITORY;${arg}")
      set(packageType "git")
    else()
      # Give up
      message(FATAL_ERROR "${CPM_INDENT} Can't determine package type of '${arg}'")
    endif()
  endif()

  # For all packages we interpret @... as version. Only replace the last occurrence. Thus URIs
  # containing '@' can be used
  string(REGEX REPLACE "@([^@]+)$" ";VERSION;\\1" out "${out}")

  # Parse the rest according to package type
  if(packageType STREQUAL "git")
    # For git repos we interpret #... as a tag or branch or commit hash
    string(REGEX REPLACE "#([^#]+)$" ";GIT_TAG;\\1" out "${out}")
  elseif(packageType STREQUAL "archive")
    # For archives we interpret #... as a URL hash.
    string(REGEX REPLACE "#([^#]+)$" ";URL_HASH;\\1" out "${out}")
    # We don't try to parse the version if it's not provided explicitly. cpm_get_version_from_url
    # should do this at a later point
  else()
    # We should never get here. This is an assertion and hitting it means there's a problem with the
    # code above. A packageType was set, but not handled by this if-else.
    message(FATAL_ERROR "${CPM_INDENT} Unsupported package type '${packageType}' of '${arg}'")
  endif()

  set(${outArgs}
      ${out}
      PARENT_SCOPE
  )
endfunction()

# Check that the working directory for a git repo is clean
function(cpm_check_git_working_dir_is_clean repoPath gitTag isClean)

  find_package(Git REQUIRED)

  if(NOT GIT_EXECUTABLE)
    # No git executable, assume directory is clean
    set(${isClean}
        TRUE
        PARENT_SCOPE
    )
    return()
  endif()

  # check for uncommitted changes
  execute_process(
    COMMAND ${GIT_EXECUTABLE} status --porcelain
    RESULT_VARIABLE resultGitStatus
    OUTPUT_VARIABLE repoStatus
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    WORKING_DIRECTORY ${repoPath}
  )
  if(resultGitStatus)
    # not supposed to happen, assume clean anyway
    message(WARNING "${CPM_INDENT} Calling git status on folder ${repoPath} failed")
    set(${isClean}
        TRUE
        PARENT_SCOPE
    )
    return()
  endif()

  if(NOT "${repoStatus}" STREQUAL "")
    set(${isClean}
        FALSE
        PARENT_SCOPE
    )
    return()
  endif()

  # check for committed changes
  execute_process(
    COMMAND ${GIT_EXECUTABLE} diff -s --exit-code ${gitTag}
    RESULT_VARIABLE resultGitDiff
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_QUIET
    WORKING_DIRECTORY ${repoPath}
  )

  if(${resultGitDiff} EQUAL 0)
    set(${isClean}
        TRUE
        PARENT_SCOPE
    )
  else()
    set(${isClean}
        FALSE
        PARENT_SCOPE
    )
  endif()

endfunction()

# Add PATCH_COMMAND to CPM_ARGS_UNPARSED_ARGUMENTS. This method consumes a list of files in ARGN
# then generates a `PATCH_COMMAND` appropriate for `ExternalProject_Add()`. This command is appended
# to the parent scope's `CPM_ARGS_UNPARSED_ARGUMENTS`.
function(cpm_add_patches)
  # Return if no patch files are supplied.
  if(NOT ARGN)
    return()
  endif()

  # Find the patch program.
  find_program(PATCH_EXECUTABLE patch)
  if(CMAKE_HOST_WIN32 AND NOT PATCH_EXECUTABLE)
    # The Windows git executable is distributed with patch.exe. Find the path to the executable, if
    # it exists, then search `../usr/bin` and `../../usr/bin` for patch.exe.
    find_package(Git QUIET)
    if(GIT_EXECUTABLE)
      get_filename_component(extra_search_path ${GIT_EXECUTABLE} DIRECTORY)
      get_filename_component(extra_search_path_1up ${extra_search_path} DIRECTORY)
      get_filename_component(extra_search_path_2up ${extra_search_path_1up} DIRECTORY)
      find_program(
        PATCH_EXECUTABLE patch HINTS "${extra_search_path_1up}/usr/bin"
                                     "${extra_search_path_2up}/usr/bin"
      )
    endif()
  endif()
  if(NOT PATCH_EXECUTABLE)
    message(FATAL_ERROR "Couldn't find `patch` executable to use with PATCHES keyword.")
  endif()

  # Create a temporary
  set(temp_list ${CPM_ARGS_UNPARSED_ARGUMENTS})

  # Ensure each file exists (or error out) and add it to the list.
  set(first_item True)
  foreach(PATCH_FILE ${ARGN})
    # Make sure the patch file exists, if we can't find it, try again in the current directory.
    if(NOT EXISTS "${PATCH_FILE}")
      if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${PATCH_FILE}")
        message(FATAL_ERROR "Couldn't find patch file: '${PATCH_FILE}'")
      endif()
      set(PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/${PATCH_FILE}")
    endif()

    # Convert to absolute path for use with patch file command.
    get_filename_component(PATCH_FILE "${PATCH_FILE}" ABSOLUTE)

    # The first patch entry must be preceded by "PATCH_COMMAND" while the following items are
    # preceded by "&&".
    if(first_item)
      set(first_item False)
      list(APPEND temp_list "PATCH_COMMAND")
    else()
      list(APPEND temp_list "&&")
    endif()
    # Add the patch command to the list
    list(APPEND temp_list "${PATCH_EXECUTABLE}" "-p1" "<" "${PATCH_FILE}")
  endforeach()

  # Move temp out into parent scope.
  set(CPM_ARGS_UNPARSED_ARGUMENTS
      ${temp_list}
      PARENT_SCOPE
  )

endfunction()

# method to overwrite internal FetchContent properties, to allow using CPM.cmake to overload
# FetchContent calls. As these are internal cmake properties, this method should be used carefully
# and may need modification in future CMake versions. Source:
# https://github.com/Kitware/CMake/blob/dc3d0b5a0a7d26d43d6cfeb511e224533b5d188f/Modules/FetchContent.cmake#L1152
function(cpm_override_fetchcontent contentName)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE_DIR;BINARY_DIR" "")
  if(NOT "${arg_UNPARSED_ARGUMENTS}" STREQUAL "")
    message(FATAL_ERROR "${CPM_INDENT} Unsupported arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()

  string(TOLOWER ${contentName} contentNameLower)
  set(prefix "_FetchContent_${contentNameLower}")

  set(propertyName "${prefix}_sourceDir")
  define_property(
    GLOBAL
    PROPERTY ${propertyName}
    BRIEF_DOCS "Internal implementation detail of FetchContent_Populate()"
    FULL_DOCS "Details used by FetchContent_Populate() for ${contentName}"
  )
  set_property(GLOBAL PROPERTY ${propertyName} "${arg_SOURCE_DIR}")

  set(propertyName "${prefix}_binaryDir")
  define_property(
    GLOBAL
    PROPERTY ${propertyName}
    BRIEF_DOCS "Internal implementation detail of FetchContent_Populate()"
    FULL_DOCS "Details used by FetchContent_Populate() for ${contentName}"
  )
  set_property(GLOBAL PROPERTY ${propertyName} "${arg_BINARY_DIR}")

  set(propertyName "${prefix}_populated")
  define_property(
    GLOBAL
    PROPERTY ${propertyName}
    BRIEF_DOCS "Internal implementation detail of FetchContent_Populate()"
    FULL_DOCS "Details used by FetchContent_Populate() for ${contentName}"
  )
  set_property(GLOBAL PROPERTY ${propertyName} TRUE)
endfunction()

# Download and add a package from source
function(CPMAddPackage)
  cpm_set_policies()

  set(oneValueArgs
      NAME
      FORCE
      VERSION
      GIT_TAG
      DOWNLOAD_ONLY
      GITHUB_REPOSITORY
      GITLAB_REPOSITORY
      BITBUCKET_REPOSITORY
      GIT_REPOSITORY
      SOURCE_DIR
      FIND_PACKAGE_ARGUMENTS
      NO_CACHE
      SYSTEM
      GIT_SHALLOW
      EXCLUDE_FROM_ALL
      SOURCE_SUBDIR
      CUSTOM_CACHE_KEY
  )

  set(multiValueArgs URL OPTIONS DOWNLOAD_COMMAND PATCHES)

  list(LENGTH ARGN argnLength)

  # Parse single shorthand argument
  if(argnLength EQUAL 1)
    cpm_parse_add_package_single_arg("${ARGN}" ARGN)

    # The shorthand syntax implies EXCLUDE_FROM_ALL and SYSTEM
    set(ARGN "${ARGN};EXCLUDE_FROM_ALL;YES;SYSTEM;YES;")

    # Parse URI shorthand argument
  elseif(argnLength GREATER 1 AND "${ARGV0}" STREQUAL "URI")
    list(REMOVE_AT ARGN 0 1) # remove "URI gh:<...>@version#tag"
    cpm_parse_add_package_single_arg("${ARGV1}" ARGV0)

    set(ARGN "${ARGV0};EXCLUDE_FROM_ALL;YES;SYSTEM;YES;${ARGN}")
  endif()

  cmake_parse_arguments(CPM_ARGS "" "${oneValueArgs}" "${multiValueArgs}" "${ARGN}")

  # Set default values for arguments
  if(NOT DEFINED CPM_ARGS_VERSION)
    if(DEFINED CPM_ARGS_GIT_TAG)
      cpm_get_version_from_git_tag("${CPM_ARGS_GIT_TAG}" CPM_ARGS_VERSION)
    endif()
  endif()

  if(CPM_ARGS_DOWNLOAD_ONLY)
    set(DOWNLOAD_ONLY ${CPM_ARGS_DOWNLOAD_ONLY})
  else()
    set(DOWNLOAD_ONLY NO)
  endif()

  if(DEFINED CPM_ARGS_GITHUB_REPOSITORY)
    set(CPM_ARGS_GIT_REPOSITORY "https://github.com/${CPM_ARGS_GITHUB_REPOSITORY}.git")
  elseif(DEFINED CPM_ARGS_GITLAB_REPOSITORY)
    set(CPM_ARGS_GIT_REPOSITORY "https://gitlab.com/${CPM_ARGS_GITLAB_REPOSITORY}.git")
  elseif(DEFINED CPM_ARGS_BITBUCKET_REPOSITORY)
    set(CPM_ARGS_GIT_REPOSITORY "https://bitbucket.org/${CPM_ARGS_BITBUCKET_REPOSITORY}.git")
  endif()

  if(DEFINED CPM_ARGS_GIT_REPOSITORY)
    list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS GIT_REPOSITORY ${CPM_ARGS_GIT_REPOSITORY})
    if(NOT DEFINED CPM_ARGS_GIT_TAG)
      set(CPM_ARGS_GIT_TAG v${CPM_ARGS_VERSION})
    endif()

    # If a name wasn't provided, try to infer it from the git repo
    if(NOT DEFINED CPM_ARGS_NAME)
      cpm_package_name_from_git_uri(${CPM_ARGS_GIT_REPOSITORY} CPM_ARGS_NAME)
    endif()
  endif()

  set(CPM_SKIP_FETCH FALSE)

  if(DEFINED CPM_ARGS_GIT_TAG)
    list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS GIT_TAG ${CPM_ARGS_GIT_TAG})
    # If GIT_SHALLOW is explicitly specified, honor the value.
    if(DEFINED CPM_ARGS_GIT_SHALLOW)
      list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS GIT_SHALLOW ${CPM_ARGS_GIT_SHALLOW})
    endif()
  endif()

  if(DEFINED CPM_ARGS_URL)
    # If a name or version aren't provided, try to infer them from the URL
    list(GET CPM_ARGS_URL 0 firstUrl)
    cpm_package_name_and_ver_from_url(${firstUrl} nameFromUrl verFromUrl)
    # If we fail to obtain name and version from the first URL, we could try other URLs if any.
    # However multiple URLs are expected to be quite rare, so for now we won't bother.

    # If the caller provided their own name and version, they trump the inferred ones.
    if(NOT DEFINED CPM_ARGS_NAME)
      set(CPM_ARGS_NAME ${nameFromUrl})
    endif()
    if(NOT DEFINED CPM_ARGS_VERSION)
      set(CPM_ARGS_VERSION ${verFromUrl})
    endif()

    list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS URL "${CPM_ARGS_URL}")
  endif()

  # Check for required arguments

  if(NOT DEFINED CPM_ARGS_NAME)
    message(
      FATAL_ERROR
        "${CPM_INDENT} 'NAME' was not provided and couldn't be automatically inferred for package added with arguments: '${ARGN}'"
    )
  endif()

  # Check if package has been added before
  cpm_check_if_package_already_added(${CPM_ARGS_NAME} "${CPM_ARGS_VERSION}")
  if(CPM_PACKAGE_ALREADY_ADDED)
    cpm_export_variables(${CPM_ARGS_NAME})
    return()
  endif()

  # Check for manual overrides
  if(NOT CPM_ARGS_FORCE AND NOT "${CPM_${CPM_ARGS_NAME}_SOURCE}" STREQUAL "")
    set(PACKAGE_SOURCE ${CPM_${CPM_ARGS_NAME}_SOURCE})
    set(CPM_${CPM_ARGS_NAME}_SOURCE "")
    CPMAddPackage(
      NAME "${CPM_ARGS_NAME}"
      SOURCE_DIR "${PACKAGE_SOURCE}"
      EXCLUDE_FROM_ALL "${CPM_ARGS_EXCLUDE_FROM_ALL}"
      SYSTEM "${CPM_ARGS_SYSTEM}"
      PATCHES "${CPM_ARGS_PATCHES}"
      OPTIONS "${CPM_ARGS_OPTIONS}"
      SOURCE_SUBDIR "${CPM_ARGS_SOURCE_SUBDIR}"
      DOWNLOAD_ONLY "${DOWNLOAD_ONLY}"
      FORCE True
    )
    cpm_export_variables(${CPM_ARGS_NAME})
    return()
  endif()

  # Check for available declaration
  if(NOT CPM_ARGS_FORCE AND NOT "${CPM_DECLARATION_${CPM_ARGS_NAME}}" STREQUAL "")
    set(declaration ${CPM_DECLARATION_${CPM_ARGS_NAME}})
    set(CPM_DECLARATION_${CPM_ARGS_NAME} "")
    CPMAddPackage(${declaration})
    cpm_export_variables(${CPM_ARGS_NAME})
    # checking again to ensure version and option compatibility
    cpm_check_if_package_already_added(${CPM_ARGS_NAME} "${CPM_ARGS_VERSION}")
    return()
  endif()

  if(NOT CPM_ARGS_FORCE)
    if(CPM_USE_LOCAL_PACKAGES OR CPM_LOCAL_PACKAGES_ONLY)
      cpm_find_package(${CPM_ARGS_NAME} "${CPM_ARGS_VERSION}" ${CPM_ARGS_FIND_PACKAGE_ARGUMENTS})

      if(CPM_PACKAGE_FOUND)
        cpm_export_variables(${CPM_ARGS_NAME})
        return()
      endif()

      if(CPM_LOCAL_PACKAGES_ONLY)
        message(
          SEND_ERROR
            "${CPM_INDENT} ${CPM_ARGS_NAME} not found via find_package(${CPM_ARGS_NAME} ${CPM_ARGS_VERSION})"
        )
      endif()
    endif()
  endif()

  CPMRegisterPackage("${CPM_ARGS_NAME}" "${CPM_ARGS_VERSION}")

  if(DEFINED CPM_ARGS_GIT_TAG)
    set(PACKAGE_INFO "${CPM_ARGS_GIT_TAG}")
  elseif(DEFINED CPM_ARGS_SOURCE_DIR)
    set(PACKAGE_INFO "${CPM_ARGS_SOURCE_DIR}")
  else()
    set(PACKAGE_INFO "${CPM_ARGS_VERSION}")
  endif()

  if(DEFINED FETCHCONTENT_BASE_DIR)
    # respect user's FETCHCONTENT_BASE_DIR if set
    set(CPM_FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR})
  else()
    set(CPM_FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps)
  endif()

  cpm_add_patches(${CPM_ARGS_PATCHES})

  if(DEFINED CPM_ARGS_DOWNLOAD_COMMAND)
    list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS DOWNLOAD_COMMAND ${CPM_ARGS_DOWNLOAD_COMMAND})
  elseif(DEFINED CPM_ARGS_SOURCE_DIR)
    list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS SOURCE_DIR ${CPM_ARGS_SOURCE_DIR})
    if(NOT IS_ABSOLUTE ${CPM_ARGS_SOURCE_DIR})
      # Expand `CPM_ARGS_SOURCE_DIR` relative path. This is important because EXISTS doesn't work
      # for relative paths.
      get_filename_component(
        source_directory ${CPM_ARGS_SOURCE_DIR} REALPATH BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}
      )
    else()
      set(source_directory ${CPM_ARGS_SOURCE_DIR})
    endif()
    if(NOT EXISTS ${source_directory})
      string(TOLOWER ${CPM_ARGS_NAME} lower_case_name)
      # remove timestamps so CMake will re-download the dependency
      file(REMOVE_RECURSE "${CPM_FETCHCONTENT_BASE_DIR}/${lower_case_name}-subbuild")
    endif()
  elseif(CPM_SOURCE_CACHE AND NOT CPM_ARGS_NO_CACHE)
    string(TOLOWER ${CPM_ARGS_NAME} lower_case_name)
    set(origin_parameters ${CPM_ARGS_UNPARSED_ARGUMENTS})
    list(SORT origin_parameters)
    if(CPM_ARGS_CUSTOM_CACHE_KEY)
      # Application set a custom unique directory name
      set(download_directory ${CPM_SOURCE_CACHE}/${lower_case_name}/${CPM_ARGS_CUSTOM_CACHE_KEY})
    elseif(CPM_USE_NAMED_CACHE_DIRECTORIES)
      string(SHA1 origin_hash "${origin_parameters};NEW_CACHE_STRUCTURE_TAG")
      cpm_get_shortest_hash(
        "${CPM_SOURCE_CACHE}/${lower_case_name}" # source cache directory
        "${origin_hash}" # Input hash
        origin_hash # Computed hash
      )
      set(download_directory ${CPM_SOURCE_CACHE}/${lower_case_name}/${origin_hash}/${CPM_ARGS_NAME})
    else()
      string(SHA1 origin_hash "${origin_parameters}")
      cpm_get_shortest_hash(
        "${CPM_SOURCE_CACHE}/${lower_case_name}" # source cache directory
        "${origin_hash}" # Input hash
        origin_hash # Computed hash
      )
      set(download_directory ${CPM_SOURCE_CACHE}/${lower_case_name}/${origin_hash})
    endif()
    # Expand `download_directory` relative path. This is important because EXISTS doesn't work for
    # relative paths.
    get_filename_component(download_directory ${download_directory} ABSOLUTE)
    list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS SOURCE_DIR ${download_directory})

    if(CPM_SOURCE_CACHE)
      file(LOCK ${download_directory}/../cmake.lock)
    endif()

    if(EXISTS ${download_directory})
      if(CPM_SOURCE_CACHE)
        file(LOCK ${download_directory}/../cmake.lock RELEASE)
      endif()

      cpm_store_fetch_properties(
        ${CPM_ARGS_NAME} "${download_directory}"
        "${CPM_FETCHCONTENT_BASE_DIR}/${lower_case_name}-build"
      )
      cpm_get_fetch_properties("${CPM_ARGS_NAME}")

      if(DEFINED CPM_ARGS_GIT_TAG AND NOT (PATCH_COMMAND IN_LIST CPM_ARGS_UNPARSED_ARGUMENTS))
        # warn if cache has been changed since checkout
        cpm_check_git_working_dir_is_clean(${download_directory} ${CPM_ARGS_GIT_TAG} IS_CLEAN)
        if(NOT ${IS_CLEAN})
          message(
            WARNING "${CPM_INDENT} Cache for ${CPM_ARGS_NAME} (${download_directory}) is dirty"
          )
        endif()
      endif()

      cpm_add_subdirectory(
        "${CPM_ARGS_NAME}"
        "${DOWNLOAD_ONLY}"
        "${${CPM_ARGS_NAME}_SOURCE_DIR}/${CPM_ARGS_SOURCE_SUBDIR}"
        "${${CPM_ARGS_NAME}_BINARY_DIR}"
        "${CPM_ARGS_EXCLUDE_FROM_ALL}"
        "${CPM_ARGS_SYSTEM}"
        "${CPM_ARGS_OPTIONS}"
      )
      set(PACKAGE_INFO "${PACKAGE_INFO} at ${download_directory}")

      # As the source dir is already cached/populated, we override the call to FetchContent.
      set(CPM_SKIP_FETCH TRUE)
      cpm_override_fetchcontent(
        "${lower_case_name}" SOURCE_DIR "${${CPM_ARGS_NAME}_SOURCE_DIR}/${CPM_ARGS_SOURCE_SUBDIR}"
        BINARY_DIR "${${CPM_ARGS_NAME}_BINARY_DIR}"
      )

    else()
      # Enable shallow clone when GIT_TAG is not a commit hash. Our guess may not be accurate, but
      # it should guarantee no commit hash get mis-detected.
      if(NOT DEFINED CPM_ARGS_GIT_SHALLOW)
        cpm_is_git_tag_commit_hash("${CPM_ARGS_GIT_TAG}" IS_HASH)
        if(NOT ${IS_HASH})
          list(APPEND CPM_ARGS_UNPARSED_ARGUMENTS GIT_SHALLOW TRUE)
        endif()
      endif()

      # remove timestamps so CMake will re-download the dependency
      file(REMOVE_RECURSE ${CPM_FETCHCONTENT_BASE_DIR}/${lower_case_name}-subbuild)
      set(PACKAGE_INFO "${PACKAGE_INFO} to ${download_directory}")
    endif()
  endif()

  if(NOT "${DOWNLOAD_ONLY}")
    cpm_create_module_file(${CPM_ARGS_NAME} "CPMAddPackage(\"${ARGN}\")")
  endif()

  if(CPM_PACKAGE_LOCK_ENABLED)
    if((CPM_ARGS_VERSION AND NOT CPM_ARGS_SOURCE_DIR) OR CPM_INCLUDE_ALL_IN_PACKAGE_LOCK)
      cpm_add_to_package_lock(${CPM_ARGS_NAME} "${ARGN}")
    elseif(CPM_ARGS_SOURCE_DIR)
      cpm_add_comment_to_package_lock(${CPM_ARGS_NAME} "local directory")
    else()
      cpm_add_comment_to_package_lock(${CPM_ARGS_NAME} "${ARGN}")
    endif()
  endif()

  cpm_message(
    STATUS "${CPM_INDENT} Adding package ${CPM_ARGS_NAME}@${CPM_ARGS_VERSION} (${PACKAGE_INFO})"
  )

  if(NOT CPM_SKIP_FETCH)
    # CMake 3.28 added EXCLUDE, SYSTEM (3.25), and SOURCE_SUBDIR (3.18) to FetchContent_Declare.
    # Calling FetchContent_MakeAvailable will then internally forward these options to
    # add_subdirectory. Up until these changes, we had to call FetchContent_Populate and
    # add_subdirectory separately, which is no longer necessary and has been deprecated as of 3.30.
    # A Bug in CMake prevents us to use the non-deprecated functions until 3.30.3.
    set(fetchContentDeclareExtraArgs "")
    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.30.3")
      if(${CPM_ARGS_EXCLUDE_FROM_ALL})
        list(APPEND fetchContentDeclareExtraArgs EXCLUDE_FROM_ALL)
      endif()
      if(${CPM_ARGS_SYSTEM})
        list(APPEND fetchContentDeclareExtraArgs SYSTEM)
      endif()
      if(DEFINED CPM_ARGS_SOURCE_SUBDIR)
        list(APPEND fetchContentDeclareExtraArgs SOURCE_SUBDIR ${CPM_ARGS_SOURCE_SUBDIR})
      endif()
      # For CMake version <3.28 OPTIONS are parsed in cpm_add_subdirectory
      if(CPM_ARGS_OPTIONS AND NOT DOWNLOAD_ONLY)
        foreach(OPTION ${CPM_ARGS_OPTIONS})
          cpm_parse_option("${OPTION}")
          set(${OPTION_KEY} "${OPTION_VALUE}")
        endforeach()
      endif()
    endif()
    cpm_declare_fetch(
      "${CPM_ARGS_NAME}" ${fetchContentDeclareExtraArgs} "${CPM_ARGS_UNPARSED_ARGUMENTS}"
    )

    cpm_fetch_package("${CPM_ARGS_NAME}" ${DOWNLOAD_ONLY} populated ${CPM_ARGS_UNPARSED_ARGUMENTS})
    if(CPM_SOURCE_CACHE AND download_directory)
      file(LOCK ${download_directory}/../cmake.lock RELEASE)
    endif()
    if(${populated} AND ${CMAKE_VERSION} VERSION_LESS "3.30.3")
      cpm_add_subdirectory(
        "${CPM_ARGS_NAME}"
        "${DOWNLOAD_ONLY}"
        "${${CPM_ARGS_NAME}_SOURCE_DIR}/${CPM_ARGS_SOURCE_SUBDIR}"
        "${${CPM_ARGS_NAME}_BINARY_DIR}"
        "${CPM_ARGS_EXCLUDE_FROM_ALL}"
        "${CPM_ARGS_SYSTEM}"
        "${CPM_ARGS_OPTIONS}"
      )
    endif()
    cpm_get_fetch_properties("${CPM_ARGS_NAME}")
  endif()

  set(${CPM_ARGS_NAME}_ADDED YES)
  cpm_export_variables("${CPM_ARGS_NAME}")
endfunction()

# Fetch a previously declared package
macro(CPMGetPackage Name)
  if(DEFINED "CPM_DECLARATION_${Name}")
    CPMAddPackage(NAME ${Name})
  else()
    message(SEND_ERROR "${CPM_INDENT} Cannot retrieve package ${Name}: no declaration available")
  endif()
endmacro()

# export variables available to the caller to the parent scope expects ${CPM_ARGS_NAME} to be set
macro(cpm_export_variables name)
  set(${name}_SOURCE_DIR
      "${${name}_SOURCE_DIR}"
      PARENT_SCOPE
  )
  set(${name}_BINARY_DIR
      "${${name}_BINARY_DIR}"
      PARENT_SCOPE
  )
  set(${name}_ADDED
      "${${name}_ADDED}"
      PARENT_SCOPE
  )
  set(CPM_LAST_PACKAGE_NAME
      "${name}"
      PARENT_SCOPE
  )
endmacro()

# declares a package, so that any call to CPMAddPackage for the package name will use these
# arguments instead. Previous declarations will not be overridden.
macro(CPMDeclarePackage Name)
  if(NOT DEFINED "CPM_DECLARATION_${Name}")
    set("CPM_DECLARATION_${Name}" "${ARGN}")
  endif()
endmacro()

function(cpm_add_to_package_lock Name)
  if(NOT CPM_DONT_CREATE_PACKAGE_LOCK)
    cpm_prettify_package_arguments(PRETTY_ARGN false ${ARGN})
    file(APPEND ${CPM_PACKAGE_LOCK_FILE} "# ${Name}\nCPMDeclarePackage(${Name}\n${PRETTY_ARGN})\n")
  endif()
endfunction()

function(cpm_add_comment_to_package_lock Name)
  if(NOT CPM_DONT_CREATE_PACKAGE_LOCK)
    cpm_prettify_package_arguments(PRETTY_ARGN true ${ARGN})
    file(APPEND ${CPM_PACKAGE_LOCK_FILE}
         "# ${Name} (unversioned)\n# CPMDeclarePackage(${Name}\n${PRETTY_ARGN}#)\n"
    )
  endif()
endfunction()

# includes the package lock file if it exists and creates a target `cpm-update-package-lock` to
# update it
macro(CPMUsePackageLock file)
  if(NOT CPM_DONT_CREATE_PACKAGE_LOCK)
    get_filename_component(CPM_ABSOLUTE_PACKAGE_LOCK_PATH ${file} ABSOLUTE)
    if(EXISTS ${CPM_ABSOLUTE_PACKAGE_LOCK_PATH})
      include(${CPM_ABSOLUTE_PACKAGE_LOCK_PATH})
    endif()
    if(NOT TARGET cpm-update-package-lock)
      add_custom_target(
        cpm-update-package-lock COMMAND ${CMAKE_COMMAND} -E copy ${CPM_PACKAGE_LOCK_FILE}
                                        ${CPM_ABSOLUTE_PACKAGE_LOCK_PATH}
      )
    endif()
    set(CPM_PACKAGE_LOCK_ENABLED true)
  endif()
endmacro()

# registers a package that has been added to CPM
function(CPMRegisterPackage PACKAGE VERSION)
  list(APPEND CPM_PACKAGES ${PACKAGE})
  set(CPM_PACKAGES
      ${CPM_PACKAGES}
      CACHE INTERNAL ""
  )
  set("CPM_PACKAGE_${PACKAGE}_VERSION"
      ${VERSION}
      CACHE INTERNAL ""
  )
endfunction()

# retrieve the current version of the package to ${OUTPUT}
function(CPMGetPackageVersion PACKAGE OUTPUT)
  set(${OUTPUT}
      "${CPM_PACKAGE_${PACKAGE}_VERSION}"
      PARENT_SCOPE
  )
endfunction()

# declares a package in FetchContent_Declare
function(cpm_declare_fetch PACKAGE)
  if(${CPM_DRY_RUN})
    cpm_message(STATUS "${CPM_INDENT} Package not declared (dry run)")
    return()
  endif()

  FetchContent_Declare(${PACKAGE} ${ARGN})
endfunction()

# returns properties for a package previously defined by cpm_declare_fetch
function(cpm_get_fetch_properties PACKAGE)
  if(${CPM_DRY_RUN})
    return()
  endif()

  set(${PACKAGE}_SOURCE_DIR
      "${CPM_PACKAGE_${PACKAGE}_SOURCE_DIR}"
      PARENT_SCOPE
  )
  set(${PACKAGE}_BINARY_DIR
      "${CPM_PACKAGE_${PACKAGE}_BINARY_DIR}"
      PARENT_SCOPE
  )
endfunction()

function(cpm_store_fetch_properties PACKAGE source_dir binary_dir)
  if(${CPM_DRY_RUN})
    return()
  endif()

  set(CPM_PACKAGE_${PACKAGE}_SOURCE_DIR
      "${source_dir}"
      CACHE INTERNAL ""
  )
  set(CPM_PACKAGE_${PACKAGE}_BINARY_DIR
      "${binary_dir}"
      CACHE INTERNAL ""
  )
endfunction()

# adds a package as a subdirectory if viable, according to provided options
function(
  cpm_add_subdirectory
  PACKAGE
  DOWNLOAD_ONLY
  SOURCE_DIR
  BINARY_DIR
  EXCLUDE
  SYSTEM
  OPTIONS
)

  if(NOT DOWNLOAD_ONLY AND EXISTS ${SOURCE_DIR}/CMakeLists.txt)
    set(addSubdirectoryExtraArgs "")
    if(EXCLUDE)
      list(APPEND addSubdirectoryExtraArgs EXCLUDE_FROM_ALL)
    endif()
    if("${SYSTEM}" AND "${CMAKE_VERSION}" VERSION_GREATER_EQUAL "3.25")
      # https://cmake.org/cmake/help/latest/prop_dir/SYSTEM.html#prop_dir:SYSTEM
      list(APPEND addSubdirectoryExtraArgs SYSTEM)
    endif()
    if(OPTIONS)
      foreach(OPTION ${OPTIONS})
        cpm_parse_option("${OPTION}")
        set(${OPTION_KEY} "${OPTION_VALUE}")
      endforeach()
    endif()
    set(CPM_OLD_INDENT "${CPM_INDENT}")
    set(CPM_INDENT "${CPM_INDENT} ${PACKAGE}:")
    add_subdirectory(${SOURCE_DIR} ${BINARY_DIR} ${addSubdirectoryExtraArgs})
    set(CPM_INDENT "${CPM_OLD_INDENT}")
  endif()
endfunction()

# downloads a previously declared package via FetchContent and exports the variables
# `${PACKAGE}_SOURCE_DIR` and `${PACKAGE}_BINARY_DIR` to the parent scope
function(cpm_fetch_package PACKAGE DOWNLOAD_ONLY populated)
  set(${populated}
      FALSE
      PARENT_SCOPE
  )
  if(${CPM_DRY_RUN})
    cpm_message(STATUS "${CPM_INDENT} Package ${PACKAGE} not fetched (dry run)")
    return()
  endif()

  FetchContent_GetProperties(${PACKAGE})

  string(TOLOWER "${PACKAGE}" lower_case_name)

  if(NOT ${lower_case_name}_POPULATED)
    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.30.3")
      if(DOWNLOAD_ONLY)
        # MakeAvailable will call add_subdirectory internally which is not what we want when
        # DOWNLOAD_ONLY is set. Populate will only download the dependency without adding it to the
        # build
        FetchContent_Populate(
          ${PACKAGE}
          SOURCE_DIR "${CPM_FETCHCONTENT_BASE_DIR}/${lower_case_name}-src"
          BINARY_DIR "${CPM_FETCHCONTENT_BASE_DIR}/${lower_case_name}-build"
          SUBBUILD_DIR "${CPM_FETCHCONTENT_BASE_DIR}/${lower_case_name}-subbuild"
          ${ARGN}
        )
      else()
        FetchContent_MakeAvailable(${PACKAGE})
      endif()
    else()
      FetchContent_Populate(${PACKAGE})
    endif()
    set(${populated}
        TRUE
        PARENT_SCOPE
    )
  endif()

  cpm_store_fetch_properties(
    ${CPM_ARGS_NAME} ${${lower_case_name}_SOURCE_DIR} ${${lower_case_name}_BINARY_DIR}
  )

  set(${PACKAGE}_SOURCE_DIR
      ${${lower_case_name}_SOURCE_DIR}
      PARENT_SCOPE
  )
  set(${PACKAGE}_BINARY_DIR
      ${${lower_case_name}_BINARY_DIR}
      PARENT_SCOPE
  )
endfunction()

# splits a package option
function(cpm_parse_option OPTION)
  string(REGEX MATCH "^[^ ]+" OPTION_KEY "${OPTION}")
  string(LENGTH "${OPTION}" OPTION_LENGTH)
  string(LENGTH "${OPTION_KEY}" OPTION_KEY_LENGTH)
  if(OPTION_KEY_LENGTH STREQUAL OPTION_LENGTH)
    # no value for key provided, assume user wants to set option to "ON"
    set(OPTION_VALUE "ON")
  else()
    math(EXPR OPTION_KEY_LENGTH "${OPTION_KEY_LENGTH}+1")
    string(SUBSTRING "${OPTION}" "${OPTION_KEY_LENGTH}" "-1" OPTION_VALUE)
  endif()
  set(OPTION_KEY
      "${OPTION_KEY}"
      PARENT_SCOPE
  )
  set(OPTION_VALUE
      "${OPTION_VALUE}"
      PARENT_SCOPE
  )
endfunction()

# guesses the package version from a git tag
function(cpm_get_version_from_git_tag GIT_TAG RESULT)
  string(LENGTH ${GIT_TAG} length)
  if(length EQUAL 40)
    # GIT_TAG is probably a git hash
    set(${RESULT}
        0
        PARENT_SCOPE
    )
  else()
    string(REGEX MATCH "v?([0123456789.]*).*" _ ${GIT_TAG})
    set(${RESULT}
        ${CMAKE_MATCH_1}
        PARENT_SCOPE
    )
  endif()
endfunction()

# guesses if the git tag is a commit hash or an actual tag or a branch name.
function(cpm_is_git_tag_commit_hash GIT_TAG RESULT)
  string(LENGTH "${GIT_TAG}" length)
  # full hash has 40 characters, and short hash has at least 7 characters.
  if(length LESS 7 OR length GREATER 40)
    set(${RESULT}
        0
        PARENT_SCOPE
    )
  else()
    if(${GIT_TAG} MATCHES "^[a-fA-F0-9]+$")
      set(${RESULT}
          1
          PARENT_SCOPE
      )
    else()
      set(${RESULT}
          0
          PARENT_SCOPE
      )
    endif()
  endif()
endfunction()

function(cpm_prettify_package_arguments OUT_VAR IS_IN_COMMENT)
  set(oneValueArgs
      NAME
      FORCE
      VERSION
      GIT_TAG
      DOWNLOAD_ONLY
      GITHUB_REPOSITORY
      GITLAB_REPOSITORY
      BITBUCKET_REPOSITORY
      GIT_REPOSITORY
      SOURCE_DIR
      FIND_PACKAGE_ARGUMENTS
      NO_CACHE
      SYSTEM
      GIT_SHALLOW
      EXCLUDE_FROM_ALL
      SOURCE_SUBDIR
  )
  set(multiValueArgs URL OPTIONS DOWNLOAD_COMMAND)
  cmake_parse_arguments(CPM_ARGS "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  foreach(oneArgName ${oneValueArgs})
    if(DEFINED CPM_ARGS_${oneArgName})
      if(${IS_IN_COMMENT})
        string(APPEND PRETTY_OUT_VAR "#")
      endif()
      if(${oneArgName} STREQUAL "SOURCE_DIR")
        string(REPLACE ${CMAKE_SOURCE_DIR} "\${CMAKE_SOURCE_DIR}" CPM_ARGS_${oneArgName}
                       ${CPM_ARGS_${oneArgName}}
        )
      endif()
      string(APPEND PRETTY_OUT_VAR "  ${oneArgName} ${CPM_ARGS_${oneArgName}}\n")
    endif()
  endforeach()
  foreach(multiArgName ${multiValueArgs})
    if(DEFINED CPM_ARGS_${multiArgName})
      if(${IS_IN_COMMENT})
        string(APPEND PRETTY_OUT_VAR "#")
      endif()
      string(APPEND PRETTY_OUT_VAR "  ${multiArgName}\n")
      foreach(singleOption ${CPM_ARGS_${multiArgName}})
        if(${IS_IN_COMMENT})
          string(APPEND PRETTY_OUT_VAR "#")
        endif()
        string(APPEND PRETTY_OUT_VAR "    \"${singleOption}\"\n")
      endforeach()
    endif()
  endforeach()

  if(NOT "${CPM_ARGS_UNPARSED_ARGUMENTS}" STREQUAL "")
    if(${IS_IN_COMMENT})
      string(APPEND PRETTY_OUT_VAR "#")
    endif()
    string(APPEND PRETTY_OUT_VAR " ")
    foreach(CPM_ARGS_UNPARSED_ARGUMENT ${CPM_ARGS_UNPARSED_ARGUMENTS})
      string(APPEND PRETTY_OUT_VAR " ${CPM_ARGS_UNPARSED_ARGUMENT}")
    endforeach()
    string(APPEND PRETTY_OUT_VAR "\n")
  endif()

  set(${OUT_VAR}
      ${PRETTY_OUT_VAR}
      PARENT_SCOPE
  )

endfunction()
