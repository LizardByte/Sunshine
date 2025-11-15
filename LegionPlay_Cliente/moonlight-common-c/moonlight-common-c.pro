#-------------------------------------------------
#
# Project created by QtCreator 2018-05-05T17:41:00
#
#-------------------------------------------------

QT -= core gui

TARGET = moonlight-common-c
TEMPLATE = lib

# Build a static library
CONFIG += staticlib

# Include global qmake defs
include(../globaldefs.pri)

win32 {
    contains(QT_ARCH, i386) {
        INCLUDEPATH += $$PWD/../libs/windows/include/x86
    }
    contains(QT_ARCH, x86_64) {
        INCLUDEPATH += $$PWD/../libs/windows/include/x64
    }
    contains(QT_ARCH, arm64) {
        INCLUDEPATH += $$PWD/../libs/windows/include/arm64
    }

    INCLUDEPATH += $$PWD/../libs/windows/include
    DEFINES += HAS_QOS_FLOWID=1 HAS_PQOS_FLOWID=1
}
macx {
    INCLUDEPATH += $$PWD/../libs/mac/include
}
unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += openssl
    DEFINES += HAVE_CLOCK_GETTIME=1
}

COMMON_C_DIR = $$PWD/moonlight-common-c
ENET_DIR = $$COMMON_C_DIR/enet
RS_DIR = $$COMMON_C_DIR/reedsolomon
SOURCES += \
    $$RS_DIR/rs.c \
    $$ENET_DIR/callbacks.c \
    $$ENET_DIR/compress.c \
    $$ENET_DIR/host.c \
    $$ENET_DIR/list.c \
    $$ENET_DIR/packet.c \
    $$ENET_DIR/peer.c \
    $$ENET_DIR/protocol.c \
    $$ENET_DIR/unix.c \
    $$ENET_DIR/win32.c \
    $$COMMON_C_DIR/src/AudioStream.c \
    $$COMMON_C_DIR/src/ByteBuffer.c \
    $$COMMON_C_DIR/src/Connection.c \
    $$COMMON_C_DIR/src/ConnectionTester.c \
    $$COMMON_C_DIR/src/ControlStream.c \
    $$COMMON_C_DIR/src/FakeCallbacks.c \
    $$COMMON_C_DIR/src/InputStream.c \
    $$COMMON_C_DIR/src/LinkedBlockingQueue.c \
    $$COMMON_C_DIR/src/Misc.c \
    $$COMMON_C_DIR/src/Platform.c \
    $$COMMON_C_DIR/src/PlatformCrypto.c \
    $$COMMON_C_DIR/src/PlatformSockets.c \
    $$COMMON_C_DIR/src/RtpAudioQueue.c \
    $$COMMON_C_DIR/src/RtpVideoQueue.c \
    $$COMMON_C_DIR/src/RtspConnection.c \
    $$COMMON_C_DIR/src/RtspParser.c \
    $$COMMON_C_DIR/src/SdpGenerator.c \
    $$COMMON_C_DIR/src/SimpleStun.c \
    $$COMMON_C_DIR/src/VideoDepacketizer.c \
    $$COMMON_C_DIR/src/VideoStream.c
HEADERS += \
    $$COMMON_C_DIR/src/Limelight.h
INCLUDEPATH += \
    $$RS_DIR \
    $$ENET_DIR/include \
    $$COMMON_C_DIR/src
DEFINES += HAS_SOCKLEN_T

CONFIG(debug, debug|release) {
    # Enable asserts on debug builds
    DEFINES += LC_DEBUG
}

# Older GCC versions defaulted to GNU89
*-g++ {
    QMAKE_CFLAGS += -std=gnu99
}

# Disable unused parameter warnings on GCC and Clang
*-g++|*-clang* {
    QMAKE_CFLAGS_WARN_ON += -Wno-unused-parameter
}
