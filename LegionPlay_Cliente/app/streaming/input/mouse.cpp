#include "input.h"

#include <Limelight.h>
#include "SDL_compat.h"
#include "streaming/streamutils.h"

void SdlInputHandler::handleMouseButtonEvent(SDL_MouseButtonEvent* event)
{
    int button;

    if (event->which == SDL_TOUCH_MOUSEID) {
        // Ignore synthetic mouse events
        return;
    }
    else if (!isCaptureActive()) {
        if (event->button == SDL_BUTTON_LEFT && event->state == SDL_RELEASED &&
                isMouseInVideoRegion(event->x, event->y)) {
            // Capture the mouse again if clicked when unbound.
            // We start capture on left button released instead of
            // pressed to avoid sending an errant mouse button released
            // event to the host when clicking into our window (since
            // the pressed event was consumed by this code).
            setCaptureActive(true);
        }

        // Not capturing
        return;
    }
    else if (m_AbsoluteMouseMode && !isMouseInVideoRegion(event->x, event->y) && event->state == SDL_PRESSED) {
        // Ignore button presses outside the video region, but allow button releases
        return;
    }

    switch (event->button)
    {
        case SDL_BUTTON_LEFT:
            button = BUTTON_LEFT;
            break;
        case SDL_BUTTON_MIDDLE:
            button = BUTTON_MIDDLE;
            break;
        case SDL_BUTTON_RIGHT:
            button = BUTTON_RIGHT;
            break;
        case SDL_BUTTON_X1:
            button = BUTTON_X1;
            break;
        case SDL_BUTTON_X2:
            button = BUTTON_X2;
            break;
        default:
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Unhandled button event: %d",
                        event->button);
            return;
    }

    if (m_SwapMouseButtons) {
        if (button == BUTTON_RIGHT)
            button = BUTTON_LEFT;
        else if (button == BUTTON_LEFT)
            button = BUTTON_RIGHT;
    }

    LiSendMouseButtonEvent(event->state == SDL_PRESSED ?
                               BUTTON_ACTION_PRESS :
                               BUTTON_ACTION_RELEASE,
                           button);
}

void SdlInputHandler::handleMouseMotionEvent(SDL_MouseMotionEvent* event)
{
    if (!isCaptureActive()) {
        // Not capturing
        return;
    }
    else if (event->which == SDL_TOUCH_MOUSEID) {
        // Ignore synthetic mouse events
        return;
    }

    // Batch all pending mouse motion events to save CPU time
    Sint32 x = event->x, y = event->y, xrel = event->xrel, yrel = event->yrel;
    SDL_Event nextEvent;
    while (SDL_PeepEvents(&nextEvent, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) > 0) {
        event = &nextEvent.motion;

        // Ignore synthetic mouse events
        if (event->which != SDL_TOUCH_MOUSEID) {
            x = event->x;
            y = event->y;
            xrel += event->xrel;
            yrel += event->yrel;
        }
    }

    // We should not reference the original event anymore
    event = nullptr;

    if (m_AbsoluteMouseMode) {
        int windowWidth, windowHeight;
        SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);

        SDL_Rect src, dst;
        bool mouseInVideoRegion;

        src.x = src.y = 0;
        src.w = m_StreamWidth;
        src.h = m_StreamHeight;

        dst.x = dst.y = 0;
        dst.w = windowWidth;
        dst.h = windowHeight;

        // Use the stream and window sizes to determine the video region
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        mouseInVideoRegion = isMouseInVideoRegion(x, y, windowWidth, windowHeight);

        // Clamp motion to the video region
        x = qMin(qMax(x - dst.x, 0), dst.w);
        y = qMin(qMax(y - dst.y, 0), dst.h);

        // Send the mouse position update if one of the following is true:
        // a) it is in the video region now
        // b) it just left the video region (to ensure the mouse is clamped to the video boundary)
        // c) a mouse button is still down from before the cursor left the video region (to allow smooth dragging)
        Uint32 buttonState = SDL_GetMouseState(nullptr, nullptr);
        if (buttonState == 0) {
            if (m_PendingMouseButtonsAllUpOnVideoRegionLeave) {
                // Stop capturing the mouse now
                SDL_CaptureMouse(SDL_FALSE);
                m_PendingMouseButtonsAllUpOnVideoRegionLeave = false;
            }
        }
        if (mouseInVideoRegion || m_MouseWasInVideoRegion || m_PendingMouseButtonsAllUpOnVideoRegionLeave) {
            LiSendMousePositionEvent((short)x, (short)y, dst.w, dst.h);
        }

        // Adjust the cursor visibility if applicable
        if (mouseInVideoRegion ^ m_MouseWasInVideoRegion) {
            SDL_ShowCursor((mouseInVideoRegion && m_MouseCursorCapturedVisibilityState == SDL_DISABLE) ? SDL_DISABLE : SDL_ENABLE);
            if (!mouseInVideoRegion && buttonState != 0) {
                // If we still have a button pressed on leave, wait for that to come up
                // before we stop sending mouse position events.
                m_PendingMouseButtonsAllUpOnVideoRegionLeave = true;
            }
        }

        m_MouseWasInVideoRegion = mouseInVideoRegion;
    }
    else {
        LiSendMouseMoveEvent(xrel, yrel);
    }
}

void SdlInputHandler::handleMouseWheelEvent(SDL_MouseWheelEvent* event)
{
    if (!isCaptureActive()) {
        // Not capturing
        return;
    }
    else if (event->which == SDL_TOUCH_MOUSEID) {
        // Ignore synthetic mouse events
        return;
    }

    if (m_AbsoluteMouseMode) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        if (!isMouseInVideoRegion(mouseX, mouseY)) {
            // Ignore scroll events outside the video region
            return;
        }
    }

#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (event->preciseY != 0.0f) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->preciseY = -event->preciseY;
        }

#ifdef Q_OS_DARWIN
        // HACK: Clamp the scroll values on macOS to prevent OS scroll acceleration
        // from generating wild scroll deltas when scrolling quickly.
        event->preciseY = SDL_clamp(event->preciseY, -1.0f, 1.0f);
#endif

        LiSendHighResScrollEvent((short)(event->preciseY * 120)); // WHEEL_DELTA
    }

    if (event->preciseX != 0.0f) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->preciseX = -event->preciseY;
        }

#ifdef Q_OS_DARWIN
        // HACK: Clamp the scroll values on macOS to prevent OS scroll acceleration
        // from generating wild scroll deltas when scrolling quickly.
        event->preciseX = SDL_clamp(event->preciseX, -1.0f, 1.0f);
#endif

        LiSendHighResHScrollEvent((short)(event->preciseX * 120)); // WHEEL_DELTA
    }
#else
    if (event->y != 0) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->y = -event->y;
        }

#ifdef Q_OS_DARWIN
        // See comment above
        event->y = SDL_clamp(event->y, -1, 1);
#endif

        LiSendScrollEvent((signed char)event->y);
    }

    if (event->x != 0) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->x = -event->x;
        }

#ifdef Q_OS_DARWIN
        // See comment above
        event->x = SDL_clamp(event->x, -1, 1);
#endif

        LiSendHScrollEvent((signed char)event->x);
    }
#endif
}

bool SdlInputHandler::isMouseInVideoRegion(int mouseX, int mouseY, int windowWidth, int windowHeight)
{
    SDL_Rect src, dst;

    if (windowWidth < 0 || windowHeight < 0) {
        SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);
    }

    src.x = src.y = 0;
    src.w = m_StreamWidth;
    src.h = m_StreamHeight;

    dst.x = dst.y = 0;
    dst.w = windowWidth;
    dst.h = windowHeight;

    // Use the stream and window sizes to determine the video region
    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    return (mouseX >= dst.x && mouseX <= dst.x + dst.w) &&
           (mouseY >= dst.y && mouseY <= dst.y + dst.h);
}

void SdlInputHandler::updatePointerRegionLock()
{
    // Pointer region lock is irrelevant in relative mouse mode
    if (SDL_GetRelativeMouseMode()) {
        return;
    }

    // Our pointer lock behavior tracks with the fullscreen mode unless the user has
    // toggled it themselves using the keyboard shortcut. If that's the case, they
    // have full control over it and we don't touch it anymore.
    if (!m_PointerRegionLockToggledByUser) {
        // Lock the pointer in true full-screen mode or in any fullscreen mode when only a single monitor is present
        Uint32 fullscreenFlags = SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        m_PointerRegionLockActive = (fullscreenFlags == SDL_WINDOW_FULLSCREEN) ||
                                    (fullscreenFlags != 0 && SDL_GetNumVideoDisplays() == 1);
    }

    // If region lock is enabled, grab the cursor so it can't accidentally leave our window.
    if (isCaptureActive() && m_PointerRegionLockActive) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_Rect src, dst;

        src.x = src.y = 0;
        src.w = m_StreamWidth;
        src.h = m_StreamHeight;

        dst.x = dst.y = 0;
        SDL_GetWindowSize(m_Window, &dst.w, &dst.h);

        // Use the stream and window sizes to determine the video region
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        // SDL 2.0.18 lets us lock the cursor to a specific region
        SDL_SetWindowMouseRect(m_Window, &dst);
#elif SDL_VERSION_ATLEAST(2, 0, 15)
        // SDL 2.0.15 only lets us lock the cursor to the whole window
        SDL_SetWindowMouseGrab(m_Window, SDL_TRUE);
#else
        SDL_SetWindowGrab(m_Window, SDL_TRUE);
#endif
    }
    else {
        // Allow the cursor to leave the bounds of our video region or window
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_SetWindowMouseRect(m_Window, nullptr);
#elif SDL_VERSION_ATLEAST(2, 0, 15)
        SDL_SetWindowMouseGrab(m_Window, SDL_FALSE);
#else
        SDL_SetWindowGrab(m_Window, SDL_FALSE);
#endif
    }
}
