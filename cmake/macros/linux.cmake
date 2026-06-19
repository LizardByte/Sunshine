# linux specific macros

# GEN_WAYLAND: args = `filename`
macro(GEN_WAYLAND wayland_directory subdirectory filename)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated-src)

    message("wayland-scanner private-code \
${wayland_directory}/${subdirectory}/${filename}.xml \
${CMAKE_BINARY_DIR}/generated-src/${filename}.c")
    message("wayland-scanner client-header \
${wayland_directory}/${subdirectory}/${filename}.xml \
${CMAKE_BINARY_DIR}/generated-src/${filename}.h")
    execute_process(
            COMMAND wayland-scanner private-code
            ${wayland_directory}/${subdirectory}/${filename}.xml
            ${CMAKE_BINARY_DIR}/generated-src/${filename}.c
            COMMAND wayland-scanner client-header
            ${wayland_directory}/${subdirectory}/${filename}.xml
            ${CMAKE_BINARY_DIR}/generated-src/${filename}.h

            RESULT_VARIABLE EXIT_INT
    )

    if(NOT ${EXIT_INT} EQUAL 0)
        message(FATAL_ERROR "wayland-scanner failed")
    endif()

    list(APPEND PLATFORM_TARGET_FILES
            ${CMAKE_BINARY_DIR}/generated-src/${filename}.c
            ${CMAKE_BINARY_DIR}/generated-src/${filename}.h)
endmacro()
