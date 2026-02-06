# windows specific dependencies

# MinHook setup - use installed minhook for AMD64, otherwise download minhook-detours for ARM64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64")
    # Make sure MinHook is installed for x86/x64
    find_library(MINHOOK_LIBRARY libMinHook.a REQUIRED)
    find_path(MINHOOK_INCLUDE_DIR MinHook.h PATH_SUFFIXES include REQUIRED)

    add_library(minhook::minhook STATIC IMPORTED)
    set_property(TARGET minhook::minhook PROPERTY IMPORTED_LOCATION ${MINHOOK_LIBRARY})
    target_include_directories(minhook::minhook INTERFACE ${MINHOOK_INCLUDE_DIR})
else()
    # Download pre-built minhook-detours for ARM64
    message(STATUS "Downloading minhook-detours pre-built binaries for ARM64")
    include(FetchContent)

    FetchContent_Declare(
        minhook-detours
        URL      https://github.com/m417z/minhook-detours/releases/download/v1.0.6/minhook-detours-1.0.6.zip
        URL_HASH SHA256=E719959D824511E27395A82AEDA994CAAD53A67EE5894BA5FC2F4BF1FA41E38E
    )
    FetchContent_MakeAvailable(minhook-detours)

    # Create imported library for the pre-built DLL
    set(_MINHOOK_DLL
        "${minhook-detours_SOURCE_DIR}/Release/minhook-detours.ARM64.Release.dll"
        CACHE INTERNAL "Path to minhook-detours DLL")
    add_library(minhook::minhook SHARED IMPORTED GLOBAL)
    set_property(TARGET minhook::minhook PROPERTY IMPORTED_LOCATION "${_MINHOOK_DLL}")
    set_property(TARGET minhook::minhook PROPERTY IMPORTED_IMPLIB
        "${minhook-detours_SOURCE_DIR}/Release/minhook-detours.ARM64.Release.lib")
    set_target_properties(minhook::minhook PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${minhook-detours_SOURCE_DIR}/src"
    )
endif()
