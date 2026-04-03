#pragma once

#ifdef CLOCK91_MODE

// Gesture actions passed from bl.cpp to clock91
enum Clock91Gesture {
    CLOCK91_GESTURE_NONE = 0,
    CLOCK91_GESTURE_PREV,       // left tap or swipe ← → previous station
    CLOCK91_GESTURE_NEXT,       // right tap or swipe → → next station
    CLOCK91_GESTURE_TAP_MIDDLE, // middle tap → timer start/cancel
};

// Run one clock91 wake cycle.
// Called from bl_init() after common hardware init (display, preferences,
// IQS323). WiFi reset and OTG toggle are handled before this call.
// gesture: the touch action detected on this wake (NONE if timer wake).
void clock91_cycle(Clock91Gesture gesture);

#endif // CLOCK91_MODE
