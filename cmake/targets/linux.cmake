# linux specific target definitions

if(NOT FREEBSD)
    # Using newer c++ compilers / features on older distros causes runtime dyn link errors
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
            -static-libgcc
            -static-libstdc++
    )
endif()
