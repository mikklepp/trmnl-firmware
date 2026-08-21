#include "alarm.h"
#include "trmnl_log.h"

#define CURFEW_HOUR   20
#define CURFEW_MINUTE 59

AlarmState calculateAlarm(int sunset_hour, int sunset_minute,
                          int current_hour, int current_minute) {
    AlarmState state = {};
    state.hour = -1;
    state.triggered = false;

    // Sunset minus 1 minute
    int sunset_total = sunset_hour * 60 + sunset_minute - 1;
    int curfew_total = CURFEW_HOUR * 60 + CURFEW_MINUTE;

    int alarm_total;

    if (sunset_hour < 0) {
        // No valid sunset (polar night/midnight sun) — use curfew only
        alarm_total = curfew_total;
    } else {
        alarm_total = (sunset_total < curfew_total) ? sunset_total : curfew_total;
    }

    if (alarm_total < 0) alarm_total += 1440;

    state.hour = alarm_total / 60;
    state.minute = alarm_total % 60;
    state.triggered = (current_hour == state.hour && current_minute == state.minute);

    Log_info("Alarm: %02d:%02d (sunset-1=%d curfew=%d) triggered=%d",
             state.hour, state.minute, sunset_total, curfew_total, state.triggered);
    return state;
}
