#include "sunset.h"
#include "trmnl_log.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float degToRad(float d) { return d * (float)M_PI / 180.0f; }
static float radToDeg(float r) { return r * 180.0f / (float)M_PI; }

// Day of year (1-366)
static int dayOfYear(int year, int month, int day) {
    static const int days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int doy = days[month - 1] + day;
    // Leap year
    if (month > 2 && (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0)) {
        doy++;
    }
    return doy;
}

// Julian century from Julian day
static double julianCentury(double jd) {
    return (jd - 2451545.0) / 36525.0;
}

// Julian day from calendar date
static double julianDay(int year, int month, int day) {
    if (month <= 2) { year--; month += 12; }
    int A = year / 100;
    int B = 2 - A + A / 4;
    return (int)(365.25 * (year + 4716)) + (int)(30.6001 * (month + 1)) + day + B - 1524.5;
}

SunTimes calculateSunTimes(int year, int month, int day, float lat, float lon) {
    SunTimes result = {};
    result.valid = false;

    double jd = julianDay(year, month, day);
    double T = julianCentury(jd);

    // Sun's geometric mean longitude (degrees)
    double L0 = fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);

    // Sun's mean anomaly (degrees)
    double M = fmod(357.52911 + T * (35999.05029 - T * 0.0001537), 360.0);
    double Mrad = M * M_PI / 180.0;

    // Equation of center
    double C = sin(Mrad) * (1.9146 - T * (0.004817 + T * 0.000014))
             + sin(2.0 * Mrad) * (0.019993 - T * 0.000101)
             + sin(3.0 * Mrad) * 0.000289;

    // Sun's true longitude
    double sunLon = L0 + C;

    // Sun's apparent longitude
    double omega = 125.04 - 1934.136 * T;
    double lambda = sunLon - 0.00569 - 0.00478 * sin(omega * M_PI / 180.0);

    // Obliquity of the ecliptic
    double epsilon0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
    double epsilon = epsilon0 + 0.00256 * cos(omega * M_PI / 180.0);

    // Sun's declination
    double sinDec = sin(epsilon * M_PI / 180.0) * sin(lambda * M_PI / 180.0);
    double decl = asin(sinDec) * 180.0 / M_PI;

    // Earth's orbital eccentricity
    double ecc = 0.016708634 - T * (0.000042037 + T * 0.0000001267);

    // Equation of time (minutes) — NOAA formula
    double y = tan(epsilon / 2.0 * M_PI / 180.0);
    y = y * y;
    double L0rad = L0 * M_PI / 180.0;
    double eqTime = 4.0 * (180.0 / M_PI) * (
        y * sin(2.0 * L0rad)
        - 2.0 * ecc * sin(Mrad)
        + 4.0 * ecc * y * sin(Mrad) * cos(2.0 * L0rad)
        - 0.5 * y * y * sin(4.0 * L0rad)
        - 1.25 * ecc * ecc * sin(2.0 * Mrad)
    );

    // Hour angle for sunrise/sunset (solar zenith = 90.833° for atmospheric refraction)
    double latRad = lat * M_PI / 180.0;
    double declRad = decl * M_PI / 180.0;
    double zenith = 90.833 * M_PI / 180.0;

    double cosHA = (cos(zenith) - sin(latRad) * sin(declRad))
                 / (cos(latRad) * cos(declRad));

    // Check for midnight sun / polar night
    if (cosHA < -1.0 || cosHA > 1.0) {
        Log_info("Sun: no rise/set (cosHA=%.4f) — polar night or midnight sun", cosHA);
        return result;
    }

    double HA = acos(cosHA) * 180.0 / M_PI;

    // Solar noon (minutes from midnight UTC)
    double solarNoon = 720.0 - 4.0 * lon - eqTime;

    // Sunrise and sunset in minutes from midnight UTC
    double sunriseMin = solarNoon - HA * 4.0;
    double sunsetMin = solarNoon + HA * 4.0;

    result.sunrise_hours = sunriseMin / 60.0f;
    result.sunset_hours = sunsetMin / 60.0f;
    result.valid = true;
    Log_info("Sun: rise=%.2fh set=%.2fh (UTC, lat=%.2f lon=%.2f)",
             result.sunrise_hours, result.sunset_hours, lat, lon);
    return result;
}

void utcToLocal(float utc_hours, float utc_offset_hours, int* hour, int* minute) {
    float local = utc_hours + utc_offset_hours;
    if (local < 0) local += 24.0f;
    if (local >= 24.0f) local -= 24.0f;
    *hour = (int)local;
    *minute = (int)((local - *hour) * 60.0f + 0.5f);
    if (*minute >= 60) { *minute = 0; (*hour)++; }
    if (*hour >= 24) *hour -= 24;
}
