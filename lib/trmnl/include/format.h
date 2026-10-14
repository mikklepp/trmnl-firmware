#pragma once

#include <cstddef>

// Format timer value as M:SS (e.g., "2:00", "0:03")
// buf must be at least 6 bytes
void formatTimer(char* buf, size_t buflen, int seconds);

// Format temperature with one decimal (e.g., "21.3", "-2.1", "0.0")
// buf must be at least 8 bytes
void formatTemp(char* buf, size_t buflen, float celsius);

// Format integer value (e.g., "145", "-28", "0")
// buf must be at least 8 bytes
void formatInt(char* buf, size_t buflen, int value);

// Rounded integer, or "-" when the value is NaN. The DSEG faces have no
// lowercase and a bare "-" reads as "no reading" on a segment display.
void formatOrDashInt(char* buf, size_t buflen, float value);

// One decimal place, or "-" when NaN. Wind speeds are reported to a tenth and
// rounding them to whole m/s loses the distinction the readings carry. Costs no
// extra width on DSEG7: its '.' has a zero advance and rides inside the
// preceding digit's cell.
//
// Single-digit values are padded to a fixed width so the decimal points line up
// down a column. The pad is FIGURE_SPACE, not ' ': DSEG7's space advances only
// 8px against a 34px digit, whereas '!' maps to an all-segments-off cell with a
// full digit advance — exactly a figure space.
#define FIGURE_SPACE '!'
void formatOrDash1(char* buf, size_t buflen, float value);

// Format float with one decimal (e.g., "12.8")
// buf must be at least 8 bytes
void formatFloat1(char* buf, size_t buflen, float value);

// Format percentage as integer (e.g., "98", "45")
void formatPct(char* buf, size_t buflen, int value);

// Format time as HH:MM (e.g., "12:34", "09:05")
// buf must be at least 6 bytes
void formatTime(char* buf, size_t buflen, int hour, int minute);

// Format date as DD-MM (e.g., "04-02", "31-12")
// buf must be at least 6 bytes
void formatDate(char* buf, size_t buflen, int day, int month);

// Format sea level with sign (e.g., "+12", "-5", "0")
// buf must be at least 6 bytes
void formatSea(char* buf, size_t buflen, int cm);

// Format 3-letter day of week from tm_wday (0=Sun)
// buf must be at least 4 bytes
void formatDow(char* buf, size_t buflen, int wday);

// Format NAN/missing as dashes "--"
// Returns true if value was NAN
bool formatOrDash(char* buf, size_t buflen, float value, void (*formatter)(char*, size_t, float));
