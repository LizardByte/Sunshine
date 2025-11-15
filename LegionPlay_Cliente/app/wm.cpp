#include <QtGlobal>
#include <QDir>

#include "utils.h"

#include "SDL_compat.h"

#ifdef HAS_X11
#include <X11/Xlib.h>
#endif

#ifdef HAS_WAYLAND
#include <wayland-client.h>
#endif

#ifdef HAVE_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

#define VALUE_SET 0x01
#define VALUE_TRUE 0x02

bool WMUtils::isRunningX11()
{
#ifdef HAS_X11
    static SDL_atomic_t isRunningOnX11;

    // If the value is not set yet, populate it now.
    int val = SDL_AtomicGet(&isRunningOnX11);
    if (!(val & VALUE_SET)) {
        Display* display = XOpenDisplay(nullptr);
        if (display != nullptr) {
            XCloseDisplay(display);
        }

        // Populate the value to return and have for next time.
        // This can race with another thread populating the same data,
        // but that's no big deal.
        val = VALUE_SET | ((display != nullptr) ? VALUE_TRUE : 0);
        SDL_AtomicSet(&isRunningOnX11, val);
    }

    return !!(val & VALUE_TRUE);
#endif

    return false;
}

bool WMUtils::isRunningWayland()
{
#ifdef HAS_WAYLAND
    static SDL_atomic_t isRunningOnWayland;

    // If the value is not set yet, populate it now.
    int val = SDL_AtomicGet(&isRunningOnWayland);
    if (!(val & VALUE_SET)) {
        struct wl_display* display = wl_display_connect(nullptr);
        if (display != nullptr) {
            wl_display_disconnect(display);
        }

        // Populate the value to return and have for next time.
        // This can race with another thread populating the same data,
        // but that's no big deal.
        val = VALUE_SET | ((display != nullptr) ? VALUE_TRUE : 0);
        SDL_AtomicSet(&isRunningOnWayland, val);
    }

    return !!(val & VALUE_TRUE);
#endif

    return false;
}

bool WMUtils::isRunningWindowManager()
{
#if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
    // Windows and macOS are always running a window manager
    return true;
#else
    // On Unix OSes, look for Wayland or X
    return WMUtils::isRunningWayland() || WMUtils::isRunningX11();
#endif
}

bool WMUtils::isRunningDesktopEnvironment()
{
    if (qEnvironmentVariableIsSet("HAS_DESKTOP_ENVIRONMENT")) {
        return qEnvironmentVariableIntValue("HAS_DESKTOP_ENVIRONMENT");
    }

#if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
    // Windows and macOS are always running a desktop environment
    return true;
#elif defined(EMBEDDED_BUILD)
    // Embedded systems don't run desktop environments
    return false;
#else
    // On non-embedded systems, assume we have a desktop environment
    // if we have a WM running.
    return isRunningWindowManager();
#endif
}

QString WMUtils::getDrmCardOverride()
{
#ifdef HAVE_DRM
    QDir dir("/dev/dri");
    QStringList cardList = dir.entryList(QStringList("card*"), QDir::Files | QDir::System);
    if (cardList.length() == 0) {
        return QString();
    }

    bool needsOverride = false;
    for (const QString& card : cardList) {
        QFile cardFd(dir.filePath(card));
        if (!cardFd.open(QFile::ReadOnly)) {
            continue;
        }

        auto resources = drmModeGetResources(cardFd.handle());
        if (resources == nullptr) {
            // If we find a card that doesn't have a display before a card that
            // has one, we'll need to override Qt's EGLFS config because they
            // don't properly handle cards without displays.
            needsOverride = true;
        }
        else {
            // We found a card with a display
            drmModeFreeResources(resources);
            if (needsOverride) {
                // Override the default card with this one
                return dir.filePath(card);
            }
            else {
                return QString();
            }
        }
    }
#endif

    return QString();
}
