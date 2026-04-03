#pragma once

#ifdef CLOCK91_MODE

#include "fmi_parse.h"
#include "forecast.h"
#include "stations.h"

// Fetch weather observations for a station.
// Returns parsed result with temperature, wind, humidity, pressure.
FmiObservations fmiFetchObservations(const FmiStation& station);

// Fetch HARMONIE wind forecast (~66h hourly) for a station's coordinates.
// Returns raw hourly arrays ready for aggregateWindForecast().
FmiForecastArrays fmiFetchWindForecast(const FmiStation& station);

// Fetch OAAS sea level forecast for a station.
// Writes hourly sea level values (cm) into out_sea[], returns count.
int fmiFetchSeaLevel(const FmiStation& station, int* out_sea, int max_hours);

#endif // CLOCK91_MODE
