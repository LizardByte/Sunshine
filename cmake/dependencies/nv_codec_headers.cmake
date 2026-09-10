# Keep build-deps-owned declarations out of Sunshine's generated package lock.
set(SUNSHINE_CPM_PACKAGE_LOCK_ENABLED "${CPM_PACKAGE_LOCK_ENABLED}")
set(CPM_PACKAGE_LOCK_ENABLED OFF)

if(NOT APPLE)
    CPMGetPackage(nv_codec_headers_13_1)
    set(NV_CODEC_HEADERS_13_1_INCLUDE_DIR "${nv_codec_headers_13_1_SOURCE_DIR}/include")
endif()

if(WIN32)
    CPMGetPackage(nv_codec_headers_13)
    CPMGetPackage(nv_codec_headers_11)
    CPMGetPackage(nv_codec_headers_12)

    set(NV_CODEC_HEADERS_11_INCLUDE_DIR "${nv_codec_headers_11_SOURCE_DIR}/include")
    set(NV_CODEC_HEADERS_12_INCLUDE_DIR "${nv_codec_headers_12_SOURCE_DIR}/include")
    set(NV_CODEC_HEADERS_13_INCLUDE_DIR "${nv_codec_headers_13_SOURCE_DIR}/include")
endif()

set(CPM_PACKAGE_LOCK_ENABLED "${SUNSHINE_CPM_PACKAGE_LOCK_ENABLED}")
unset(SUNSHINE_CPM_PACKAGE_LOCK_ENABLED)
