# linux specific compile definitions

add_compile_definitions(SUNSHINE_PLATFORM="linux")

# AppImage
if(${SUNSHINE_BUILD_APPIMAGE})
    # use relative assets path for AppImage
    string(REPLACE "${CMAKE_INSTALL_PREFIX}" ".${CMAKE_INSTALL_PREFIX}" SUNSHINE_ASSETS_DIR_DEF ${SUNSHINE_ASSETS_DIR})
endif()

if(NOT DEFINED SUNSHINE_EXECUTABLE_PATH)
    set(SUNSHINE_EXECUTABLE_PATH "sunshine")
endif()

# cuda
set(CUDA_FOUND OFF)
if(${SUNSHINE_ENABLE_CUDA})
    include(CheckLanguage)
    check_language(CUDA)

    if(CMAKE_CUDA_COMPILER)
        set(CUDA_FOUND ON)
        enable_language(CUDA)

        message(STATUS "CUDA Compiler Version: ${CMAKE_CUDA_COMPILER_VERSION}")
        set(CMAKE_CUDA_ARCHITECTURES "")

        # https://tech.amikelive.com/node-930/cuda-compatibility-of-nvidia-display-gpu-drivers/
        if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 6.5)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 10)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_10,code=sm_10")
        elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 6.5)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 50 52)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_50,code=sm_50")
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_52,code=sm_52")
        endif()

        if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 7.0)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 11)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_11,code=sm_11")
        elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER 7.6)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 60 61 62)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_60,code=sm_60")
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_61,code=sm_61")
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_62,code=sm_62")
        endif()

        if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 9.0)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 20)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_20,code=sm_20")
        elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 9.0)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 70)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_70,code=sm_70")
        endif()

        if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 10.0)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 75)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_75,code=sm_75")
        endif()

        if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 11.0)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 30)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_30,code=sm_30")
        elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 11.0)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 80)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_80,code=sm_80")
        endif()

        if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 11.1)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 86)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_86,code=sm_86")
        endif()

        if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 11.8)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 90)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_90,code=sm_90")
        endif()

        if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 12.0)
            list(APPEND CMAKE_CUDA_ARCHITECTURES 35)
            # set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_35,code=sm_35")
        endif()

        # sort the architectures
        list(SORT CMAKE_CUDA_ARCHITECTURES COMPARE NATURAL)

        # message(STATUS "CUDA NVCC Flags: ${CUDA_NVCC_FLAGS}")
        message(STATUS "CUDA Architectures: ${CMAKE_CUDA_ARCHITECTURES}")
    endif()
endif()
if(CUDA_FOUND)
    include_directories(SYSTEM third-party/nvfbc)
    list(APPEND PLATFORM_TARGET_FILES
            src/platform/linux/cuda.h
            src/platform/linux/cuda.cu
            src/platform/linux/cuda.cpp
            third-party/nvfbc/NvFBC.h)

    add_compile_definitions(SUNSHINE_BUILD_CUDA)
endif()

# drm
if(${SUNSHINE_ENABLE_DRM})
    find_package(LIBDRM)
    find_package(LIBCAP)
else()
    set(LIBDRM_FOUND OFF)
    set(LIBCAP_FOUND OFF)
endif()
if(LIBDRM_FOUND AND LIBCAP_FOUND)
    add_compile_definitions(SUNSHINE_BUILD_DRM)
    include_directories(SYSTEM ${LIBDRM_INCLUDE_DIRS} ${LIBCAP_INCLUDE_DIRS})
    list(APPEND PLATFORM_LIBRARIES ${LIBDRM_LIBRARIES} ${LIBCAP_LIBRARIES})
    list(APPEND PLATFORM_TARGET_FILES
            src/platform/linux/kmsgrab.cpp)
    list(APPEND SUNSHINE_DEFINITIONS EGL_NO_X11=1)
elseif(NOT LIBDRM_FOUND)
    message(WARNING "Missing libdrm")
elseif(NOT LIBDRM_FOUND)
    message(WARNING "Missing libcap")
endif()

# vaapi
if(${SUNSHINE_ENABLE_VAAPI})
    find_package(Libva)
else()
    set(LIBVA_FOUND OFF)
endif()
if(LIBVA_FOUND)
    add_compile_definitions(SUNSHINE_BUILD_VAAPI)
    include_directories(SYSTEM ${LIBVA_INCLUDE_DIR})
    list(APPEND PLATFORM_LIBRARIES ${LIBVA_LIBRARIES} ${LIBVA_DRM_LIBRARIES})
    list(APPEND PLATFORM_TARGET_FILES
            src/platform/linux/vaapi.h
            src/platform/linux/vaapi.cpp)
endif()

# wayland
if(${SUNSHINE_ENABLE_WAYLAND})
    find_package(Wayland)
else()
    set(WAYLAND_FOUND OFF)
endif()
if(WAYLAND_FOUND)
    add_compile_definitions(SUNSHINE_BUILD_WAYLAND)

    if(NOT SUNSHINE_SYSTEM_WAYLAND_PROTOCOLS)
        set(WAYLAND_PROTOCOLS_DIR "${CMAKE_SOURCE_DIR}/third-party/wayland-protocols")
    else()
        pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
        pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols REQUIRED)
    endif()

    GEN_WAYLAND("${WAYLAND_PROTOCOLS_DIR}" "unstable/xdg-output" xdg-output-unstable-v1)
    GEN_WAYLAND("${CMAKE_SOURCE_DIR}/third-party/wlr-protocols" "unstable" wlr-export-dmabuf-unstable-v1)

    include_directories(
            SYSTEM
            ${WAYLAND_INCLUDE_DIRS}
            ${CMAKE_BINARY_DIR}/generated-src
    )

    list(APPEND PLATFORM_LIBRARIES ${WAYLAND_LIBRARIES})
    list(APPEND PLATFORM_TARGET_FILES
            src/platform/linux/wlgrab.cpp
            src/platform/linux/wayland.h
            src/platform/linux/wayland.cpp)
endif()

# x11
if(${SUNSHINE_ENABLE_X11})
    find_package(X11)
else()
    set(X11_FOUND OFF)
endif()
if(X11_FOUND)
    add_compile_definitions(SUNSHINE_BUILD_X11)
    include_directories(SYSTEM ${X11_INCLUDE_DIR})
    list(APPEND PLATFORM_LIBRARIES ${X11_LIBRARIES})
    list(APPEND PLATFORM_TARGET_FILES
            src/platform/linux/x11grab.h
            src/platform/linux/x11grab.cpp)
endif()

if(NOT ${CUDA_FOUND}
        AND NOT ${WAYLAND_FOUND}
        AND NOT ${X11_FOUND}
        AND NOT (${LIBDRM_FOUND} AND ${LIBCAP_FOUND})
        AND NOT ${LIBVA_FOUND})
    message(FATAL_ERROR "Couldn't find either cuda, wayland, x11, (libdrm and libcap), or libva")
endif()

# tray icon
if(${SUNSHINE_ENABLE_TRAY})
    pkg_check_modules(APPINDICATOR appindicator3-0.1)
    if(APPINDICATOR_FOUND)
        list(APPEND SUNSHINE_DEFINITIONS TRAY_LEGACY_APPINDICATOR=1)
    else()
        pkg_check_modules(APPINDICATOR ayatana-appindicator3-0.1)
        if(APPINDICATOR_FOUND)
            list(APPEND SUNSHINE_DEFINITIONS TRAY_AYATANA_APPINDICATOR=1)
        endif ()
    endif()
    pkg_check_modules(LIBNOTIFY libnotify)
    if(NOT APPINDICATOR_FOUND OR NOT LIBNOTIFY_FOUND)
        set(SUNSHINE_TRAY 0)
        message(WARNING "Missing appindicator or libnotify, disabling tray icon")
        message(STATUS "APPINDICATOR_FOUND: ${APPINDICATOR_FOUND}")
        message(STATUS "LIBNOTIFY_FOUND: ${LIBNOTIFY_FOUND}")
    else()
        include_directories(SYSTEM ${APPINDICATOR_INCLUDE_DIRS} ${LIBNOTIFY_INCLUDE_DIRS})
        link_directories(${APPINDICATOR_LIBRARY_DIRS} ${LIBNOTIFY_LIBRARY_DIRS})

        list(APPEND PLATFORM_TARGET_FILES third-party/tray/tray_linux.c)
        list(APPEND SUNSHINE_EXTERNAL_LIBRARIES ${APPINDICATOR_LIBRARIES} ${LIBNOTIFY_LIBRARIES})
    endif()
else()
    set(SUNSHINE_TRAY 0)
    message(STATUS "Tray icon disabled")
endif()

if (${SUNSHINE_TRAY} EQUAL 0 AND SUNSHINE_REQUIRE_TRAY)
    message(FATAL_ERROR "Tray icon is required")
endif()

list(APPEND PLATFORM_TARGET_FILES
        src/platform/linux/publish.cpp
        src/platform/linux/graphics.h
        src/platform/linux/graphics.cpp
        src/platform/linux/misc.h
        src/platform/linux/misc.cpp
        src/platform/linux/audio.cpp
        src/platform/linux/input.cpp
        third-party/glad/src/egl.c
        third-party/glad/src/gl.c
        third-party/glad/include/EGL/eglplatform.h
        third-party/glad/include/KHR/khrplatform.h
        third-party/glad/include/glad/gl.h
        third-party/glad/include/glad/egl.h)

list(APPEND PLATFORM_LIBRARIES
        Boost::dynamic_linking
        dl
        evdev
        numa
        pulse
        pulse-simple)

include_directories(
        SYSTEM
        /usr/include/libevdev-1.0
        third-party/nv-codec-headers/include
        third-party/glad/include)
