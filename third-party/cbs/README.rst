Overview
---------
These source files are copied from FFmpeg's avcodec and avutil libraries. Internally, sunshine does stream and packet processing (see `cbs.cpp <https://github.com/LizardByte/Sunshine/blob/master/src/cbs.cpp>`) that isn't exposed by FFmpeg. This project enables that functionality.

Modified files
--------------
These files have had import paths changed or otherwise modified as noted in the file:

- avcodec.h
- cbs_av1.c
- cbs_h2645.c
- cbs_internal.h
- cbs_jpeg.c
- cbs_mpeg2.c
- cbs_sei.c
- cbs.c
- h264_levels.c
- h2645_parse.c
- intmath.h
- log2_tab.c
- put_bits.h
- config.h
- get_bits.h
