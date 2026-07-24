# linux specific target definitions

if(NOT FREEBSD)
    # Using newer c++ compilers / features on older distros causes runtime dyn link errors
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES -static-libgcc)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
        list(APPEND SUNSHINE_EXTERNAL_LIBRARIES stdc++)
    else()
        list(APPEND SUNSHINE_EXTERNAL_LIBRARIES -static-libstdc++)
    endif()
endif()
