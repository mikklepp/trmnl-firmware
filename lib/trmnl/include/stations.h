#pragma once

struct FmiStation {
    const char* name;
    const char* fmisid;
    float lat;
    float lon;
    bool has_wave;
};

// Curated list of Finnish coastal stations.
// Ordered southwest → northeast along the coast,
// with Kaisaniemi as the sole inland reference station at the end.
static const FmiStation STATIONS[] = {
    // Archipelago Sea & Åland
    {"Kumlinge",         "100928", 60.258f, 20.747f, false},
    {"Isokari",          "101059", 60.722f, 21.027f, false},
    {"Fagerholm",        "100924", 60.112f, 21.698f, false},
    {"Uto",              "100908", 59.779f, 21.375f, true},
    // Southwest coast
    {"Vano",             "100945", 59.869f, 22.193f, false},
    {"Russaro",          "100932", 59.773f, 22.946f, true},
    {"Hanko",            "100946", 59.822f, 22.977f, false},
    // Gulf of Finland
    {"Makiluoto",        "100997", 59.920f, 24.350f, false},
    {"Bagaskar",         "100969", 59.932f, 24.012f, false},
    {"Harmaja",          "100996", 60.105f, 24.975f, true},
    {"Itatoukki",        "105392", 60.101f, 25.194f, false},
    {"Emasalo",          "101023", 60.204f, 25.625f, true},
    {"Kilpilahti",       "101028", 60.303f, 25.548f, false},
    {"Orrengrund",       "101039", 60.269f, 26.445f, false},
    {"Kotka Rankki",     "101042", 60.381f, 26.964f, true},
    // Bothnian Sea
    {"Kylmapihlaja",     "101061", 61.145f, 21.303f, false},
    {"Tahkoluoto",       "101267", 61.630f, 21.376f, false},
    // Bothnian Bay
    {"Vaasa",            "101485", 63.083f, 21.617f, false},
    {"Pietarsaari",      "101481", 63.700f, 22.700f, false},
    {"Kokkola",          "101494", 63.838f, 23.048f, false},
    {"Kalajoki",         "101503", 64.250f, 23.833f, false},
    {"Oulu",             "101799", 65.012f, 25.465f, false},
    {"Hailuoto",         "101784", 65.028f, 24.660f, true},
    {"Kemi",             "101846", 65.783f, 24.550f, false},
    // Inland reference
    {"Kaisaniemi",       "100971", 60.175f, 24.944f, false},
};

static const int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);
