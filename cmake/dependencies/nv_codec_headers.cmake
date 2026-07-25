if(WIN32)
    CPMGetPackage(nv_codec_headers_13)
    CPMGetPackage(nv_codec_headers_11)
    CPMGetPackage(nv_codec_headers_12)

    set(NV_CODEC_HEADERS_11_INCLUDE_DIR "${nv_codec_headers_11_SOURCE_DIR}/include")
    set(NV_CODEC_HEADERS_12_INCLUDE_DIR "${nv_codec_headers_12_SOURCE_DIR}/include")
    set(NV_CODEC_HEADERS_13_INCLUDE_DIR "${nv_codec_headers_13_SOURCE_DIR}/include")
endif()
