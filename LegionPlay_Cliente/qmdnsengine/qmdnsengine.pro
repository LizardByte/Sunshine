QT -= gui
QT += network

TARGET = qmdnsengine
TEMPLATE = lib

# Build a static library
CONFIG += staticlib

# Disable warnings
CONFIG += warn_off

# C++11 is required to build
CONFIG += c++11

# Include global qmake defs
include(../globaldefs.pri)

QMDNSE_DIR = $$PWD/qmdnsengine/src
DEFINES += \
    QT_NO_SIGNALS_SLOTS_KEYWORDS

# These are all required to be defined here not just
# to get them loaded into the IDE nicely, but more
# importantly, to get MOC to run on them properly.
HEADERS += \
    $$QMDNSE_DIR/include/qmdnsengine/abstractserver.h  \
    $$QMDNSE_DIR/include/qmdnsengine/bitmap.h          \
    $$QMDNSE_DIR/include/qmdnsengine/browser.h         \
    $$QMDNSE_DIR/include/qmdnsengine/cache.h           \
    $$QMDNSE_DIR/include/qmdnsengine/dns.h             \
    $$QMDNSE_DIR/include/qmdnsengine/hostname.h        \
    $$QMDNSE_DIR/include/qmdnsengine/mdns.h            \
    $$QMDNSE_DIR/include/qmdnsengine/message.h         \
    $$QMDNSE_DIR/include/qmdnsengine/prober.h          \
    $$QMDNSE_DIR/include/qmdnsengine/provider.h        \
    $$QMDNSE_DIR/include/qmdnsengine/query.h           \
    $$QMDNSE_DIR/include/qmdnsengine/record.h          \
    $$QMDNSE_DIR/include/qmdnsengine/resolver.h        \
    $$QMDNSE_DIR/include/qmdnsengine/server.h          \
    $$QMDNSE_DIR/include/qmdnsengine/service.h         \
    $$QMDNSE_DIR/src/bitmap_p.h            \
    $$QMDNSE_DIR/src/browser_p.h           \
    $$QMDNSE_DIR/src/cache_p.h             \
    $$QMDNSE_DIR/src/hostname_p.h          \
    $$QMDNSE_DIR/src/message_p.h           \
    $$QMDNSE_DIR/src/prober_p.h            \
    $$QMDNSE_DIR/src/provider_p.h          \
    $$QMDNSE_DIR/src/query_p.h             \
    $$QMDNSE_DIR/src/record_p.h            \
    $$QMDNSE_DIR/src/resolver_p.h          \
    $$QMDNSE_DIR/src/server_p.h            \
    $$QMDNSE_DIR/src/service_p.h

SOURCES += \
    $$QMDNSE_DIR/src/abstractserver.cpp \
    $$QMDNSE_DIR/src/bitmap.cpp         \
    $$QMDNSE_DIR/src/browser.cpp        \
    $$QMDNSE_DIR/src/cache.cpp          \
    $$QMDNSE_DIR/src/dns.cpp            \
    $$QMDNSE_DIR/src/hostname.cpp       \
    $$QMDNSE_DIR/src/mdns.cpp           \
    $$QMDNSE_DIR/src/message.cpp        \
    $$QMDNSE_DIR/src/prober.cpp         \
    $$QMDNSE_DIR/src/provider.cpp       \
    $$QMDNSE_DIR/src/query.cpp          \
    $$QMDNSE_DIR/src/record.cpp         \
    $$QMDNSE_DIR/src/resolver.cpp       \
    $$QMDNSE_DIR/src/server.cpp         \
    $$QMDNSE_DIR/src/service.cpp

INCLUDEPATH += \
    $$QMDNSE_DIR/include \
    $$PWD/qmdnsengine
