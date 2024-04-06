if (WIN32)
elseif (APPLE)
elseif (UNIX)
    include(GNUInstallDirs)

    if(NOT DEFINED SUNSHINE_EXECUTABLE_PATH)
        set(SUNSHINE_EXECUTABLE_PATH "sunshine")
    endif()
endif ()
