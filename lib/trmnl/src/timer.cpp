#include "timer.h"
#include "trmnl_log.h"

void timerStart(TimerState* state) {
    int duration = TIMER_PRESETS[state->preset_index];
    state->remaining = duration;
    state->total = duration;
    state->buzzing = false;
    state->preset_index = (state->preset_index + 1) % TIMER_PRESET_COUNT;
    Log_info("Timer: started %ds (preset %d, next preset %d)",
             duration, (state->preset_index + TIMER_PRESET_COUNT - 1) % TIMER_PRESET_COUNT,
             state->preset_index);
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
