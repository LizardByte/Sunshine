# linux specific target definitions

# Using newer c++ compilers / features on older distros causes runtime dyn link errors
list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        -static-libgcc
        -static-libstdc++
)
