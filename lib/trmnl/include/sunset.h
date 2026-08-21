#pragma once

// Sunset calculation based on NOAA Solar Calculator equations.
// Accuracy: ±1 minute with equation of time correction.

struct SunTimes {
    float sunrise_hours;  // UTC hours (e.g., 3.5 = 03:30 UTC)
    float sunset_hours;   // UTC hours (e.g., 18.75 = 18:45 UTC)
    bool valid;           // false if midnight sun or polar night
};

// Calculate sunrise and sunset times for a given date and location.
// year/month/day: calendar date
// lat: latitude in degrees (positive = north)
// lon: longitude in degrees (positive = east)
// Returns times in UTC fractional hours.
SunTimes calculateSunTimes(int year, int month, int day, float lat, float lon);

// Convert UTC fractional hours to local hour and minute, given a UTC offset in hours.
// Example: utcToLocal(18.75, 3.0, &hour, &minute) → hour=21, minute=45
void utcToLocal(float utc_hours, float utc_offset_hours, int* hour, int* minute);
