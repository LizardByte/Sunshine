cmake_minimum_required(VERSION 3.0)

project(CBS)

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
include/cbs/codec_desc.h
include/cbs/codec_id.h
include/cbs/codec_par.h
include/cbs/config.h
include/cbs/defs.h
include/cbs/get_bits.h
include/cbs/h264_levels.h
include/cbs/h2645_parse.h
include/cbs/h264.h
include/cbs/hevc.h
include/cbs/mathops.h
include/cbs/packet.h
include/cbs/sei.h
include/cbs/vlc.h

cbs.c
cbs_h2645.c
cbs_av1.c
cbs_vp9.c
cbs_mpeg2.c
cbs_jpeg.c
cbs_sei.c
h264_levels.c
h2645_parse.c

avcodec.h
bytestream.h
cbs_internal.h
codec.h
h264_ps.h
h264_sei.h
hevc_sei.h
intmath.h
put_bits.h
version_major.h
)

include_directories(include)

if(DEFINED FFMPEG_INCLUDE_DIRS)
include_directories(${FFMPEG_INCLUDE_DIRS})
endif()

add_compile_definitions(
    HAVE_THREADS=1
    HAVE_FAST_UNALIGNED

    PIC=1

    CONFIG_CBS_AV1=1
    CONFIG_CBS_H264=1
    CONFIG_CBS_H265=1
    CONFIG_CBS_JPEG=1
    CONFIG_CBS_MPEG2=1
    CONFIG_CBS_VP9=1
    )


add_library(cbs ${CBS_SOURCE_FILES})
target_compile_options(cbs PRIVATE -Wall -Wno-incompatible-pointer-types -Wno-maybe-uninitialized -Wno-format -Wno-format-extra-args)