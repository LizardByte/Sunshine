# common macros
# this file will also load platform specific macros

# platform specific macros
if(WIN32)
    include(${CMAKE_MODULE_PATH}/macros/windows.cmake)
elseif(UNIX)
    include(${CMAKE_MODULE_PATH}/macros/unix.cmake)

    if(APPLE)
        include(${CMAKE_MODULE_PATH}/macros/macos.cmake)
    else()
        include(${CMAKE_MODULE_PATH}/macros/linux.cmake)
    endif()
endif()
