cmake_minimum_required(VERSION 3.0)

project(CBS)

SET(ARCH_SOURCE_FILES
x86/intmath.h
x86/asm.h
x86/mathops.h
)

SET(CBS_SOURCE_FILES
include/cbs/av1.h
include/cbs/cbs_av1.h
include/cbs/cbs_bsf.h
include/cbs/cbs.h
include/cbs/cbs_h2645.h
include/cbs/cbs_h264.h
include/cbs/cbs_h265.h
include/cbs/cbs_jpeg.h
include/cbs/cbs_mpeg2.h
include/cbs/cbs_sei.h
include/cbs/cbs_vp9.h
include/cbs/h2645_parse.h
include/cbs/h264.h
include/cbs/hevc.h
include/cbs/sei.h
include/cbs/h264_levels.h

cbs.c
cbs_h2645.c
cbs_av1.c
cbs_vp9.c
cbs_mpeg2.c
cbs_jpeg.c
cbs_sei.c
h2645_parse.c
h264_levels.c

attributes.h
bytestream.h
cbs_internal.h
codec_id.h
defs.h
get_bits.h
h264_ps.h
h264_sei.h
hevc_sei.h
intmath.h
mathops.h
put_bits.h
vlc.h

${ARCH_SOURCE_FILES}
)

include_directories(include)

add_compile_definitions(
    ARCH_X86=1
    ARCH_X86_64=1
    HAVE_XMM_CLOBBERS=1
    HAVE_INLINE_ASM=1
    HAVE_INLINE_ASM_DIRECT_SYMBOL_REFS=1
    HAVE_THREADS=1
    HAVE_FAST_UNALIGNED
    HAVE_FAST_CLZ=1

    HAVE_FAST_64BIT

    PIC=1

    CONFIG_CBS_AV1=1
    CONFIG_CBS_H264=1
    CONFIG_CBS_H265=1
    CONFIG_CBS_JPEG=1
    CONFIG_CBS_MPEG2=1
    CONFIG_CBS_VP9=1
    )


add_library(cbs ${CBS_SOURCE_FILES})
target_compile_options(cbs PRIVATE -Wall -Wno-incompatible-pointer-types)