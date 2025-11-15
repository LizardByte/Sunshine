#include "waylandvsyncsource.h"

#include <SDL_syswm.h>

#ifndef SDL_VIDEO_DRIVER_WAYLAND
#warning Unable to use WaylandVsyncSource without SDL support
#else

const struct wl_callback_listener WaylandVsyncSource::s_FrameListener = {
    .done = WaylandVsyncSource::frameDone,
};

WaylandVsyncSource::WaylandVsyncSource(Pacer* pacer)
    : m_Pacer(pacer),
      m_Display(nullptr),
      m_Surface(nullptr),
      m_Callback(nullptr)
{

}

WaylandVsyncSource::~WaylandVsyncSource()
{
    if (m_Callback != nullptr) {
        wl_callback_destroy(m_Callback);
        wl_display_roundtrip(m_Display);
    }
}

bool WaylandVsyncSource::initialize(SDL_Window* window, int)
{
    SDL_SysWMinfo info;

    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(window, &info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetWindowWMInfo() failed: %s",
                     SDL_GetError());
        return false;
    }

    // Pacer should not create us for non-Wayland windows
    SDL_assert(info.subsystem == SDL_SYSWM_WAYLAND);

    m_Display = info.info.wl.display;
    m_Surface = info.info.wl.surface;

    // Enqueue our first frame callback
    m_Callback = wl_surface_frame(m_Surface);
    wl_callback_add_listener(m_Callback, &s_FrameListener, this);
    wl_surface_commit(m_Surface);

    return true;
}

bool WaylandVsyncSource::isAsync()
{
    // Wayland frame callbacks are asynchronous
    return true;
}

void WaylandVsyncSource::frameDone(void* data, struct wl_callback* oldCb, uint32_t)
{
    auto me = (WaylandVsyncSource*)data;

    // Free this callback
    SDL_assert(oldCb == me->m_Callback);
    wl_callback_destroy(oldCb);

    // Wake the Pacer Vsync thread
    me->m_Pacer->signalVsync();

    // Register for another callback
    me->m_Callback = wl_surface_frame(me->m_Surface);
    wl_callback_add_listener(me->m_Callback, &s_FrameListener, data);
    wl_surface_commit(me->m_Surface);
    wl_display_flush(me->m_Display);
}

#endif
