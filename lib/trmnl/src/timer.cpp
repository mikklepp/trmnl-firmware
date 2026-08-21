#include "timer.h"
#include "trmnl_log.h"

static void applyPreset(TimerState* state) {
    int duration = TIMER_PRESETS[state->preset_index];
    state->remaining = duration;
    state->total = duration;
    state->buzzing = false;
    Log_info("Timer: %ds (preset %d)", duration, state->preset_index);
}

void timerStart(TimerState* state) {
    state->preset_index = 0;
    applyPreset(state);
}

void timerNext(TimerState* state) {
    state->preset_index = (state->preset_index + 1) % TIMER_PRESET_COUNT;
    applyPreset(state);
}

void timerPrev(TimerState* state) {
    state->preset_index = (state->preset_index - 1 + TIMER_PRESET_COUNT) % TIMER_PRESET_COUNT;
    applyPreset(state);
}

void timerCancel(TimerState* state) {
    state->remaining = -1;
    state->total = 0;
    state->buzzing = false;
}

void timerTick(TimerState* state, int delta_seconds) {
    if (state->remaining < 0) return;

    state->remaining -= delta_seconds;
    state->buzzing = false;

    if (state->remaining <= 0) {
        state->remaining = -1;
        state->buzzing = true;
        Log_info("Timer: expired, buzzing");
    }
}
