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
    if (cm > 0) {
        snprintf(buf, buflen, "+%d", cm);
    } else {
        snprintf(buf, buflen, "%d", cm);
    }
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
