# unix specific packaging
# put anything here that applies to both linux and macos

# return here if building a macos .app
if(APPLE AND NOT SUNSHINE_BUILD_HOMEBREW)
    return()
endif()

# Installation destination dir
set(CPACK_SET_DESTDIR true)
if(NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "/usr/share/sunshine")
endif()

install(TARGETS sunshine RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
