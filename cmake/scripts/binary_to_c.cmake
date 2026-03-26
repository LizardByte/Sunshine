# binary_to_c.cmake - Convert a binary file to a C uint32_t initializer list.
# Input:  SPV_FILE  - path to SPIR-V binary
# Output: OUT_FILE  - path to write C initializer (e.g. {0x07230203, ...})

file(READ "${SPV_FILE}" data HEX)
string(LENGTH "${data}" hex_len)
math(EXPR num_bytes "${hex_len} / 2")
math(EXPR num_words "${num_bytes} / 4")
math(EXPR last "${num_words} - 1")

set(out "{")
foreach(idx RANGE 0 ${last})
    math(EXPR off "${idx} * 8")
    math(EXPR off1 "${off} + 2")
    math(EXPR off2 "${off} + 4")
    math(EXPR off3 "${off} + 6")
    string(SUBSTRING "${data}" ${off} 2 b0)
    string(SUBSTRING "${data}" ${off1} 2 b1)
    string(SUBSTRING "${data}" ${off2} 2 b2)
    string(SUBSTRING "${data}" ${off3} 2 b3)
    # little-endian to uint32_t
    string(APPEND out "0x${b3}${b2}${b1}${b0}")
    if(NOT idx EQUAL last)
        string(APPEND out ",")
    endif()
    math(EXPR col "(${idx} + 1) % 8")
    if(col EQUAL 0)
        string(APPEND out "\n")
    endif()
endforeach()
string(APPEND out "}\n")

file(WRITE "${OUT_FILE}" "${out}")
