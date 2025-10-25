cmake_minimum_required(VERSION 3.20)

project(sunshine_tools)

include_directories("${CMAKE_SOURCE_DIR}")

add_executable(dxgi-info dxgi.cpp)
set_target_properties(dxgi-info PROPERTIES CXX_STANDARD 23)
target_link_libraries(dxgi-info
        ${CMAKE_THREAD_LIBS_INIT}
        dxgi
        ${PLATFORM_LIBRARIES})
target_compile_options(dxgi-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(audio-info audio.cpp)
set_target_properties(audio-info PROPERTIES CXX_STANDARD 23)
target_link_libraries(audio-info
        ${Boost_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ksuser
        ${PLATFORM_LIBRARIES})
target_compile_options(audio-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(sunshinesvc sunshinesvc.cpp)
set_target_properties(sunshinesvc PROPERTIES CXX_STANDARD 23)
target_link_libraries(sunshinesvc
        ${CMAKE_THREAD_LIBS_INIT}
        wtsapi32
        ${PLATFORM_LIBRARIES})
target_compile_options(sunshinesvc PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

# Add sunshine_wgc_capture executable
add_executable(sunshine_wgc_capture
        sunshine_wgc_capture.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/windows/ipc/pipes.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/windows/ipc/misc_utils.cpp
        ${CMAKE_SOURCE_DIR}/src/logging.cpp)
set_target_properties(sunshine_wgc_capture PROPERTIES CXX_STANDARD 23)

# Define helper build flag to conditionally compile logging
target_compile_definitions(sunshine_wgc_capture PRIVATE
        SUNSHINE_EXTERNAL_PROCESS=1
)

target_link_libraries(sunshine_wgc_capture
        ${Boost_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        d3d11
        dxgi
        ole32
        user32
        gdi32
        windowsapp
        avrt
        shcore
        wtsapi32
        ${PLATFORM_LIBRARIES})
target_compile_options(sunshine_wgc_capture PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
