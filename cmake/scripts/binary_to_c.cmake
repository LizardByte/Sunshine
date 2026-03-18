# binary_to_c.cmake - Convert a binary file to a C uint32_t initializer list.
# Input:  SPV_FILE  - path to SPIR-V binary
# Output: OUT_FILE  - path to write C initializer (e.g. {0x07230203, ...})

file(READ "${SPV_FILE}" _data HEX)
string(LENGTH "${_data}" _hex_len)
math(EXPR _num_bytes "${_hex_len} / 2")
math(EXPR _num_words "${_num_bytes} / 4")
math(EXPR _last "${_num_words} - 1")

set(_out "{")
foreach(_idx RANGE 0 ${_last})
    math(EXPR _off "${_idx} * 8")
    math(EXPR _off1 "${_off} + 2")
    math(EXPR _off2 "${_off} + 4")
    math(EXPR _off3 "${_off} + 6")
    string(SUBSTRING "${_data}" ${_off} 2 _b0)
    string(SUBSTRING "${_data}" ${_off1} 2 _b1)
    string(SUBSTRING "${_data}" ${_off2} 2 _b2)
    string(SUBSTRING "${_data}" ${_off3} 2 _b3)
    # little-endian to uint32_t
    string(APPEND _out "0x${_b3}${_b2}${_b1}${_b0}")
    if(NOT _idx EQUAL _last)
        string(APPEND _out ",")
    endif()
    math(EXPR _col "(${_idx} + 1) % 8")
    if(_col EQUAL 0)
        string(APPEND _out "\n")
    endif()
endforeach()
string(APPEND _out "}\n")

file(WRITE "${OUT_FILE}" "${_out}")
