cmake_minimum_required(VERSION 2.8)

project(sunshine_tools)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(dxgi-info dxgi.cpp)
set_target_properties(dxgi-info PROPERTIES CXX_STANDARD 17)
target_link_libraries(dxgi-info
        ${CMAKE_THREAD_LIBS_INIT}
        dxgi)
target_compile_options(dxgi-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})