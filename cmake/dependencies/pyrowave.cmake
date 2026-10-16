# Build the pinned PyroWave C API into Sunshine.
include_guard(GLOBAL)

# Build the codec and C API without exposing upstream options to Sunshine.
function(sunshine_add_pyrowave)
    set(pyrowave_source_dir "${CMAKE_SOURCE_DIR}/third-party/pyrowave")
    if(NOT EXISTS "${pyrowave_source_dir}/checkout_granite.sh")
        message(FATAL_ERROR "PyroWave submodule is missing. Run git submodule update --init --recursive.")
    endif()

    # Use the Granite revision selected by the pinned PyroWave source.
    file(STRINGS "${pyrowave_source_dir}/checkout_granite.sh" granite_revision
            REGEX "^GRANITE_COMMIT=[0-9a-f]+$")
    if(NOT granite_revision MATCHES "^GRANITE_COMMIT=([0-9a-f]+)$")
        message(FATAL_ERROR "Could not read PyroWave's pinned Granite revision.")
    endif()
    CPMAddPackage(
            NAME pyrowave_granite
            GITHUB_REPOSITORY Themaister/Granite
            GIT_TAG "${CMAKE_MATCH_1}"
            GIT_SUBMODULES third_party/volk third_party/khronos/vulkan-headers
            GIT_SUBMODULES_RECURSE OFF
            DOWNLOAD_ONLY YES)

    # Scope upstream options to the dependency build and use its precompiled shaders.
    # cmake-lint: disable=C0103
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(GRANITE_SHARED OFF)
    set(GRANITE_TOOLS OFF)
    set(GRANITE_RENDERER OFF)
    set(GRANITE_PLATFORM "null")
    set(GRANITE_INSTALL_TARGETS OFF)
    set(GRANITE_HIDDEN ON)
    set(GRANITE_POSITION_INDEPENDENT ON)
    set(GRANITE_SHIPPING ON)
    set(GRANITE_FFMPEG OFF)
    set(GRANITE_FFMPEG_VULKAN OFF)
    set(GRANITE_SHADER_COMPILER_OPTIMIZE OFF)
    set(GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER OFF)
    set(GRANITE_VULKAN_SPIRV_CROSS OFF)
    set(GRANITE_VULKAN_FOSSILIZE OFF)
    set(GRANITE_VULKAN_SYSTEM_HANDLES OFF)
    set(GRANITE_VULKAN_DXGI_INTEROP OFF)
    set(GRANITE_RENDERDOC_CAPTURE OFF)
    add_subdirectory("${pyrowave_granite_SOURCE_DIR}" "${pyrowave_granite_BINARY_DIR}" EXCLUDE_FROM_ALL)

    # Keep Volk's function pointers distinct from the Vulkan loader used by FFmpeg.
    target_compile_definitions(granite-volk-headers INTERFACE VOLK_NAMESPACE)
    set_source_files_properties("${pyrowave_granite_SOURCE_DIR}/third_party/volk/volk.c"
            TARGET_DIRECTORY granite-volk PROPERTIES LANGUAGE CXX)

    set(PYROWAVE_DEVEL OFF)
    set(PYROWAVE_UTILS OFF)
    add_subdirectory("${pyrowave_source_dir}" "${CMAKE_BINARY_DIR}/third-party/pyrowave" EXCLUDE_FROM_ALL)

    # Upstream exposes the C API only for standalone builds; add its wrapper here.
    add_library(sunshine_pyrowave STATIC
            "${pyrowave_source_dir}/pyrowave_c.cpp"
            "${pyrowave_granite_SOURCE_DIR}/video/scaler.cpp")
    target_include_directories(sunshine_pyrowave PRIVATE
            "${pyrowave_source_dir}/shaders" "${pyrowave_granite_SOURCE_DIR}/video")
    target_include_directories(sunshine_pyrowave SYSTEM INTERFACE
            "${pyrowave_source_dir}"
            "${pyrowave_granite_SOURCE_DIR}/third_party/khronos/vulkan-headers/include")
    target_link_libraries(sunshine_pyrowave PRIVATE pyrowave granite-vulkan granite-math Threads::Threads)
    configure_file("${pyrowave_source_dir}/LICENSE"
            "${CMAKE_BINARY_DIR}/pyrowave-licenses/PyroWave.txt" COPYONLY)
    configure_file("${pyrowave_granite_SOURCE_DIR}/LICENSE"
            "${CMAKE_BINARY_DIR}/pyrowave-licenses/Granite.txt" COPYONLY)
    configure_file("${pyrowave_granite_SOURCE_DIR}/third_party/volk/LICENSE.md"
            "${CMAKE_BINARY_DIR}/pyrowave-licenses/Volk.txt" COPYONLY)
    configure_file("${pyrowave_granite_SOURCE_DIR}/third_party/khronos/vulkan-headers/LICENSE.md"
            "${CMAKE_BINARY_DIR}/pyrowave-licenses/Vulkan-Headers/LICENSE.md" COPYONLY)
    foreach(license Apache-2.0 MIT)
        configure_file("${pyrowave_granite_SOURCE_DIR}/third_party/khronos/vulkan-headers/LICENSES/${license}.txt"
                "${CMAKE_BINARY_DIR}/pyrowave-licenses/Vulkan-Headers/LICENSES/${license}.txt" COPYONLY)
    endforeach()
endfunction()

sunshine_add_pyrowave()
list(APPEND SUNSHINE_EXTERNAL_LIBRARIES sunshine_pyrowave)
