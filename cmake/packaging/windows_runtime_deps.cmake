# Deploy MSYS2 runtime DLLs that are reported by ldd for a Windows binary.

if(NOT DEFINED SUNSHINE_RUNTIME_TARGET)
    message(FATAL_ERROR "SUNSHINE_RUNTIME_TARGET is required")
endif()

if(NOT DEFINED SUNSHINE_RUNTIME_OUTPUT_DIR)
    message(FATAL_ERROR "SUNSHINE_RUNTIME_OUTPUT_DIR is required")
endif()

string(REGEX REPLACE "^\"|\"$" "" SUNSHINE_RUNTIME_TARGET "${SUNSHINE_RUNTIME_TARGET}")
string(REGEX REPLACE "^\"|\"$" "" SUNSHINE_RUNTIME_OUTPUT_DIR "${SUNSHINE_RUNTIME_OUTPUT_DIR}")

find_program(SUNSHINE_LDD_EXECUTABLE ldd)
if(NOT SUNSHINE_LDD_EXECUTABLE)
    message(FATAL_ERROR "ldd is required to deploy MSYS2 runtime dependencies")
endif()

find_program(SUNSHINE_CYGPATH_EXECUTABLE cygpath)
if(NOT SUNSHINE_CYGPATH_EXECUTABLE)
    message(FATAL_ERROR "cygpath is required to deploy MSYS2 runtime dependencies")
endif()

function(sunshine_msys_path_to_cmake msys_path out_var)
    execute_process(
            COMMAND "${SUNSHINE_CYGPATH_EXECUTABLE}" -m "${msys_path}"
            OUTPUT_VARIABLE _cmake_path
            ERROR_VARIABLE _cygpath_error
            RESULT_VARIABLE _cygpath_result
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _cygpath_result EQUAL 0)
        message(FATAL_ERROR "cygpath failed for ${msys_path}: ${_cygpath_error}")
    endif()

    set(${out_var} "${_cmake_path}" PARENT_SCOPE)
endfunction()

function(sunshine_copy_msys_dependency msys_path output_dir out_path)
    sunshine_msys_path_to_cmake("${msys_path}" _source_path)
    get_filename_component(_dll_name "${_source_path}" NAME)
    set(_deployed_path "${output_dir}/${_dll_name}")

    if(NOT EXISTS "${_deployed_path}")
        execute_process(
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_source_path}"
                "${_deployed_path}"
                ERROR_VARIABLE _copy_error
                RESULT_VARIABLE _copy_result)
        if(NOT _copy_result EQUAL 0)
            message(FATAL_ERROR "Failed to copy ${_source_path}: ${_copy_error}")
        endif()
    endif()

    set(${out_path} "${_deployed_path}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE _deployed_dlls
        LIST_DIRECTORIES false
        "${SUNSHINE_RUNTIME_OUTPUT_DIR}/*.dll")

set(_pending "${SUNSHINE_RUNTIME_TARGET}" ${_deployed_dlls})
set(_processed "")

while(_pending)
    list(POP_FRONT _pending _runtime_binary)
    list(FIND _processed "${_runtime_binary}" _processed_index)
    if(NOT _processed_index EQUAL -1)
        continue()
    endif()

    list(APPEND _processed "${_runtime_binary}")

    execute_process(
            COMMAND "${SUNSHINE_LDD_EXECUTABLE}" "${_runtime_binary}"
            OUTPUT_VARIABLE _ldd_output
            ERROR_VARIABLE _ldd_error
            RESULT_VARIABLE _ldd_result)
    if(NOT _ldd_result EQUAL 0)
        message(FATAL_ERROR "ldd failed for ${_runtime_binary}: ${_ldd_error}")
    endif()

    string(REGEX MATCHALL "[^\r\n]+" _ldd_lines "${_ldd_output}")
    foreach(_ldd_line IN LISTS _ldd_lines)
        if(_ldd_line MATCHES "=>[ \t]+not[ \t]+found")
            message(FATAL_ERROR "Runtime dependency not found: ${_ldd_line}")
        endif()

        if(_ldd_line MATCHES "=>[ \t]+(/(clangarm64|clang64|mingw32|mingw64|ucrt64)/bin/[^ \t\r\n]+\\.dll)")
            sunshine_copy_msys_dependency("${CMAKE_MATCH_1}" "${SUNSHINE_RUNTIME_OUTPUT_DIR}" _deployed_dll)
            list(FIND _processed "${_deployed_dll}" _deployed_processed_index)
            list(FIND _pending "${_deployed_dll}" _deployed_pending_index)
            if(_deployed_processed_index EQUAL -1 AND _deployed_pending_index EQUAL -1)
                list(APPEND _pending "${_deployed_dll}")
            endif()
        endif()
    endforeach()
endwhile()