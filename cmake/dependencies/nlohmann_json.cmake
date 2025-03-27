#
# Loads the nlohmann_json library giving the priority to the system package first, with a fallback to FetchContent.
#
include_guard(GLOBAL)

find_package(nlohmann_json 3.11 QUIET GLOBAL)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json v3.11.x package not found in the system. Falling back to FetchContent.")
    include(FetchContent)

    FetchContent_Declare(
            json
            URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
            URL_HASH MD5=c23a33f04786d85c29fda8d16b5f0efd
            DOWNLOAD_EXTRACT_TIMESTAMP
    )
    FetchContent_MakeAvailable(json)
endif()
