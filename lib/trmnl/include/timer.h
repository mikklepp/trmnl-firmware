#pragma once

#define TIMER_PRESET_COUNT 5

struct TimerState {
    int remaining;      // seconds remaining, -1 = inactive
    int total;          // total duration of current timer
    int preset_index;   // which preset to use next (rotates)
    bool buzzing;       // true on the tick when timer hits zero
};

static const int TIMER_PRESETS[TIMER_PRESET_COUNT] = {120, 180, 240, 300, 60};

// Start the next preset timer. Rotates preset_index.
void timerStart(TimerState* state);

// Cancel a running timer.
void timerCancel(TimerState* state);

// Tick the timer by delta_seconds. Sets buzzing=true when it reaches zero.
void timerTick(TimerState* state, int delta_seconds);

// Is the timer currently active?
inline bool timerActive(const TimerState& state) { return state.remaining >= 0; }
