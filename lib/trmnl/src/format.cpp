#include "format.h"
#include <cstdio>
#include <cmath>

void formatTimer(char* buf, size_t buflen, int seconds) {
    if (seconds < 0) seconds = 0;
    int m = seconds / 60;
    int s = seconds % 60;
    snprintf(buf, buflen, "%d:%02d", m, s);
}

void formatTemp(char* buf, size_t buflen, float celsius) {
    snprintf(buf, buflen, "%.1f", celsius);
}

void formatInt(char* buf, size_t buflen, int value) {
    snprintf(buf, buflen, "%d", value);
}

void formatFloat1(char* buf, size_t buflen, float value) {
    snprintf(buf, buflen, "%.1f", value);
}

void formatPct(char* buf, size_t buflen, int value) {
    snprintf(buf, buflen, "%d", value);
}

void formatTime(char* buf, size_t buflen, int hour, int minute) {
    snprintf(buf, buflen, "%02d:%02d", hour, minute);
}

void formatDate(char* buf, size_t buflen, int day, int month) {
    snprintf(buf, buflen, "%02d-%02d", day, month);
}

void formatSea(char* buf, size_t buflen, int cm) {
    // No "+" prefix: DSEG7 is a seven-segment face and has no plus glyph — a
    // plus is not renderable in seven segments. fontconvert was asked for the
    // range 43..58, so it emitted a table entry for '+' filled with .notdef,
    // which draws as a placeholder box. (That entry has real metrics and
    // bitmap data, so measuring the font suggests the glyph is fine; only the
    // source TTF's cmap shows it absent.)
    //
    // '-' is present, so negatives still read correctly and a bare number means
    // at or above the datum.
    snprintf(buf, buflen, "%d", cm);
}

void formatDow(char* buf, size_t buflen, int wday) {
    static const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (wday >= 0 && wday <= 6) {
        snprintf(buf, buflen, "%s", days[wday]);
    } else {
        snprintf(buf, buflen, "---");
    }
}

bool formatOrDash(char* buf, size_t buflen, float value, void (*formatter)(char*, size_t, float)) {
    if (std::isnan(value)) {
        snprintf(buf, buflen, "--");
        return true;
    }
    formatter(buf, buflen, value);
    return false;
}

void formatOrDashInt(char* buf, size_t buflen, float value) {
    if (!buf || buflen == 0) return;
    if (std::isnan(value)) {
        snprintf(buf, buflen, "-");
        return;
    }
    snprintf(buf, buflen, "%d", (int)(value < 0 ? value - 0.5f : value + 0.5f));
}

void formatOrDash1(char* buf, size_t buflen, float value) {
    if (!buf || buflen == 0) return;
    if (std::isnan(value)) {
        snprintf(buf, buflen, "-");
        return;
    }
    // Pad below 10 so the decimal point sits in the same column as a two-digit
    // value. DSEG7 is monospaced, so one figure space is exactly one digit.
    if (value < 10.0f && value > -10.0f) {
        snprintf(buf, buflen, "%c%.1f", FIGURE_SPACE, (double)value);
    } else {
        snprintf(buf, buflen, "%.1f", (double)value);
    }
}
