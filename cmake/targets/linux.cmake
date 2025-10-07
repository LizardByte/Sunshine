# linux specific target definitions

# Using newer c++ compilers / features on older distros causes runtime dyn link errors
target_link_libraries(sunshine -static-libgcc -static-libstdc++)
