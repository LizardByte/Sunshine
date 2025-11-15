#include "input.h"

#include <Limelight.h>
#include "SDL_compat.h"

#include <QtMath>

// How long the mouse button will be pressed for a tap to click gesture
#define TAP_BUTTON_RELEASE_DELAY 100

// How long the fingers must be stationary to start a drag
#define DRAG_ACTIVATION_DELAY 650

// How far the finger can move before it cancels a drag or tap
#define DEAD_ZONE_DELTA 0.01f

Uint32 SdlInputHandler::releaseLeftButtonTimerCallback(Uint32, void*)
{
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    return 0;
}

Uint32 SdlInputHandler::releaseRightButtonTimerCallback(Uint32, void*)
{
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
    return 0;
}

Uint32 SdlInputHandler::dragTimerCallback(Uint32, void *param)
{
    auto me = reinterpret_cast<SdlInputHandler*>(param);

    // Check how many fingers are down now to decide
    // which button to hold down
    if (me->m_NumFingersDown == 2) {
        me->m_DragButton = BUTTON_RIGHT;
    }
    else if (me->m_NumFingersDown == 1) {
        me->m_DragButton = BUTTON_LEFT;
    }

    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, me->m_DragButton);

    return 0;
}

void SdlInputHandler::handleRelativeFingerEvent(SDL_TouchFingerEvent* event)
{
    int fingerIndex = -1;

    // Observations on Windows 10: x and y appear to be relative to 0,0 of the window client area.
    // Although SDL documentation states they are 0.0 - 1.0 float values, they can actually be higher
    // or lower than those values as touch events continue for touches started within the client area that
    // leave the client area during a drag motion.
    // dx and dy are deltas from the last touch event, not the first touch down.

    // Determine the index of this finger using our list
    // of fingers that are currently active on screen.
    // This is also required to handle finger up which
    // where the finger will not be in SDL_GetTouchFinger()
    // anymore.
    if (event->type != SDL_FINGERDOWN) {
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (event->fingerId == m_TouchDownEvent[i].fingerId) {
                fingerIndex = i;
                break;
            }
        }
    }
    else {
        // Resolve the new finger by determining the ID of each
        // finger on the display.
        int numTouchFingers = SDL_GetNumTouchFingers(event->touchId);
        for (int i = 0; i < numTouchFingers; i++) {
            SDL_Finger* finger = SDL_GetTouchFinger(event->touchId, i);
            SDL_assert(finger != nullptr);
            if (finger != nullptr) {
                if (finger->id == event->fingerId) {
                    fingerIndex = i;
                    break;
                }
            }
        }
    }

    if (fingerIndex < 0 || fingerIndex >= MAX_FINGERS) {
        // Too many fingers
        return;
    }

    // Handle cursor motion based on the position of the
    // primary finger on screen
    if (fingerIndex == 0) {
        // The event x and y values are relative to our window width
        // and height. However, we want to scale them to be relative
        // to the host resolution. Fortunately this is easy since we
        // already have normalized values. We'll just multiply them
        // by the stream dimensions to get real X and Y values rather
        // than the client window dimensions.
        short deltaX = static_cast<short>(event->dx * m_StreamWidth);
        short deltaY = static_cast<short>(event->dy * m_StreamHeight);
        if (deltaX != 0 || deltaY != 0) {
            LiSendMouseMoveEvent(deltaX, deltaY);
        }
    }

    // Start a drag timer when primary or secondary
    // fingers go down
    if (event->type == SDL_FINGERDOWN &&
            (fingerIndex == 0 || fingerIndex == 1)) {
        SDL_RemoveTimer(m_DragTimer);
        m_DragTimer = SDL_AddTimer(DRAG_ACTIVATION_DELAY,
                                   dragTimerCallback,
                                   this);
    }

    if (event->type == SDL_FINGERMOTION) {
        // If it's outside the deadzone delta, cancel drags and taps
        if (qSqrt(qPow(event->x - m_TouchDownEvent[fingerIndex].x, 2) +
                  qPow(event->y - m_TouchDownEvent[fingerIndex].y, 2)) > DEAD_ZONE_DELTA) {
            SDL_RemoveTimer(m_DragTimer);
            m_DragTimer = 0;

            // This effectively cancels the tap logic below
            m_TouchDownEvent[fingerIndex].timestamp = 0;
        }
    }

    if (event->type == SDL_FINGERUP) {
        // Cancel the drag timer on finger up
        SDL_RemoveTimer(m_DragTimer);
        m_DragTimer = 0;

        // Release any drag
        if (m_DragButton != 0) {
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, m_DragButton);
            m_DragButton = 0;
        }
        // 2 finger tap
        else if (event->timestamp - m_TouchDownEvent[1].timestamp < 250) {
            // Zero timestamp of the primary finger to ensure we won't
            // generate a left click if the primary finger comes up soon.
            m_TouchDownEvent[0].timestamp = 0;

            // Press down the right mouse button
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);

            // Queue a timer to release it in 100 ms
            SDL_RemoveTimer(m_RightButtonReleaseTimer);
            m_RightButtonReleaseTimer = SDL_AddTimer(TAP_BUTTON_RELEASE_DELAY,
                                                     releaseRightButtonTimerCallback,
                                                     nullptr);
        }
        // 1 finger tap
        else if (event->timestamp - m_TouchDownEvent[0].timestamp < 250) {
            // Press down the left mouse button
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);

            // Queue a timer to release it in 100 ms
            SDL_RemoveTimer(m_LeftButtonReleaseTimer);
            m_LeftButtonReleaseTimer = SDL_AddTimer(TAP_BUTTON_RELEASE_DELAY,
                                                    releaseLeftButtonTimerCallback,
                                                    nullptr);
        }
    }

    m_NumFingersDown = SDL_GetNumTouchFingers(event->touchId);

    if (event->type == SDL_FINGERDOWN) {
        m_TouchDownEvent[fingerIndex] = *event;
    }
    else if (event->type == SDL_FINGERUP) {
        m_TouchDownEvent[fingerIndex] = {};
    }
}
