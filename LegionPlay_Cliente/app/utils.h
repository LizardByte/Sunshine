#pragma once

#include <QString>

#define THROW_BAD_ALLOC_IF_NULL(x) \
    if ((x) == nullptr) throw std::bad_alloc()

namespace WMUtils {
    bool isRunningX11();
    bool isRunningWayland();
    bool isRunningWindowManager();
    bool isRunningDesktopEnvironment();
    QString getDrmCardOverride();
}
