#pragma once

struct AlarmState {
    int hour;       // -1 if no alarm
    int minute;
    bool triggered; // true if current time matches alarm
};

// Calculate alarm time: 1 minute before sunset or 20:59, whichever is earlier.
// sunset_hour/sunset_minute: local sunset time (-1 if invalid/polar)
// current_hour/current_minute: current local time (for trigger check)
AlarmState calculateAlarm(int sunset_hour, int sunset_minute,
                          int current_hour, int current_minute);
