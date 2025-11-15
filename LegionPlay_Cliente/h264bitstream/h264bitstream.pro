#-------------------------------------------------
#
# Project created by QtCreator 2018-10-12T15:50:59
#
#-------------------------------------------------


QT       -= core gui

TARGET = h264bitstream
TEMPLATE = lib

# Build a static library
CONFIG += staticlib

# Disable warnings
CONFIG += warn_off

# Include global qmake defs
include(../globaldefs.pri)

# Older GCC versions defaulted to GNU89
*-g++ {
    QMAKE_CFLAGS += -std=gnu99
}

SRC_DIR = $$PWD/h264bitstream

SOURCES += \
    $$SRC_DIR/h264_nal.c            \
    $$SRC_DIR/h264_sei.c            \
    $$SRC_DIR/h264_stream.c

HEADERS += \
    $$SRC_DIR/bs.h              \
    $$SRC_DIR/h264_sei.h        \
    $$SRC_DIR/h264_stream.h

INCLUDEPATH += $$INC_DIR
