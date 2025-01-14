# windows specific dependencies

# nlohmann_json
find_package(nlohmann_json CONFIG 3.11 REQUIRED)

# Make sure MinHook is installed
find_library(MINHOOK_LIBRARY minhook REQUIRED)
find_path(MINHOOK_INCLUDE_DIR MinHook.h PATH_SUFFIXES include REQUIRED)

add_library(minhook::minhook UNKNOWN IMPORTED)
set_property(TARGET minhook::minhook PROPERTY IMPORTED_LOCATION ${MINHOOK_LIBRARY})
target_include_directories(minhook::minhook INTERFACE ${MINHOOK_INCLUDE_DIR})
