#pragma once

#define TIMER_PRESET_COUNT 5

struct TimerState {
    int remaining;      // seconds remaining, -1 = inactive
    int total;          // total duration of current timer
    int preset_index;   // current preset (cycles on left/right while running)
    bool buzzing;       // true on the tick when timer hits zero
};

static const int TIMER_PRESETS[TIMER_PRESET_COUNT] = {120, 180, 240, 300, 60};

// Start timer at preset 0 (always 2:00 on first tap).
void timerStart(TimerState* state);

// Switch to next preset and restart (right tap/swipe while running).
void timerNext(TimerState* state);

// Switch to previous preset and restart (left tap/swipe while running).
void timerPrev(TimerState* state);

// Cancel a running timer (middle tap while running).
void timerCancel(TimerState* state);

// Tick the timer by delta_seconds. Sets buzzing=true when it reaches zero.
void timerTick(TimerState* state, int delta_seconds);

// Is the timer currently active?
inline bool timerActive(const TimerState& state) { return state.remaining >= 0; }
