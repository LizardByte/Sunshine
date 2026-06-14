# Qt packaging helpers

function(sunshine_find_qt_tool tool_name out_var)
    foreach(qt_major IN ITEMS 6 5)
        if(TARGET Qt${qt_major}::${tool_name})
            get_target_property(_qt_tool Qt${qt_major}::${tool_name} IMPORTED_LOCATION)
            if(_qt_tool)
                set(${out_var} "${_qt_tool}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endforeach()

    set(_qt_tool_names "${tool_name}" "${tool_name}6" "${tool_name}5")
    string(TOUPPER "${tool_name}" _qt_tool_name)
    set(_qt_tool_var "SUNSHINE_${_qt_tool_name}_EXECUTABLE")
    find_program(${_qt_tool_var} NAMES ${_qt_tool_names})
    set(${out_var} "${${_qt_tool_var}}" PARENT_SCOPE)
endfunction()

function(sunshine_require_qt_tool tool_name out_var)
    sunshine_find_qt_tool("${tool_name}" _qt_tool)
    if(NOT _qt_tool)
        message(FATAL_ERROR
                "${tool_name} is required to package Sunshine with the Qt tray backend")
    endif()

    set(${out_var} "${_qt_tool}" PARENT_SCOPE)
endfunction()

function(sunshine_deploy_qt_runtime target_name deploy_tool)
    if(NOT TARGET "${target_name}")
        return()
    endif()

    add_custom_command(TARGET "${target_name}" POST_BUILD
            COMMAND "${deploy_tool}"
            ${ARGN}
            --dir "$<TARGET_FILE_DIR:${target_name}>"
            "$<TARGET_FILE:${target_name}>"
            COMMAND "${CMAKE_COMMAND}"
            "-DSUNSHINE_RUNTIME_TARGET=$<TARGET_FILE:${target_name}>"
            "-DSUNSHINE_RUNTIME_OUTPUT_DIR=$<TARGET_FILE_DIR:${target_name}>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/windows_runtime_deps.cmake"
            COMMENT "Deploying Qt runtime for ${target_name}"
            VERBATIM)
endfunction()

function(sunshine_collect_qt_library_dirs out_var)
    set(_qt_dirs "")
    foreach(qt_major IN ITEMS 6 5)
        foreach(qt_component IN ITEMS Core Gui Widgets Svg DBus)
            if(TARGET Qt${qt_major}::${qt_component})
                set(_qt_location "")
                foreach(_qt_property IN ITEMS
                        IMPORTED_LOCATION_RELEASE
                        IMPORTED_LOCATION_RELWITHDEBINFO
                        IMPORTED_LOCATION)
                    get_target_property(_qt_property_location Qt${qt_major}::${qt_component} ${_qt_property})
                    if(_qt_property_location)
                        set(_qt_location "${_qt_property_location}")
                        break()
                    endif()
                endforeach()

                if(_qt_location)
                    get_filename_component(_qt_dir "${_qt_location}" DIRECTORY)
                    foreach(_qt_depth RANGE 1 8)
                        if(NOT _qt_dir)
                            break()
                        endif()

                        list(APPEND _qt_dirs "${_qt_dir}")
                        get_filename_component(_qt_parent "${_qt_dir}" DIRECTORY)
                        if(_qt_parent STREQUAL _qt_dir)
                            break()
                        endif()
                        set(_qt_dir "${_qt_parent}")
                    endforeach()
                endif()
            endif()
        endforeach()
    endforeach()

    if(_qt_dirs)
        list(REMOVE_DUPLICATES _qt_dirs)
    endif()
    set(${out_var} "${_qt_dirs}" PARENT_SCOPE)
endfunction()
