# linux specific target definitions

# Using newer c++ features causes runtime errors on older compilers
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    target_link_libraries(sunshine -static-libgcc -static-libstdc++)
endif()
