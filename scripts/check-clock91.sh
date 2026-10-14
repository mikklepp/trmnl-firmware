#!/usr/bin/env bash
# Static analysis for the clock91 sources.
#
# `pio check` is unusable here: the project pins check_tool = clangtidy and
# PlatformIO's tool-clangtidy package is an x86_64 binary, so on an arm64 Mac
# it dies with "Errno 86: Bad CPU type in executable". This calls Homebrew's
# native cppcheck directly instead.
#
# Suppressions, all tool configuration rather than defects:
#   unknownMacro / preprocessorErrorDirective — cppcheck doesn't expand
#     PROGMEM and without the IDF target macro it takes the #error branch
#   uninitMemberVarNoCtor — fires throughout upstream's headers
set -euo pipefail
cd "$(dirname "$0")/.."

cppcheck --enable=warning,performance,portability --inline-suppr --std=c++17 --quiet \
  --suppress=missingInclude --suppress=missingIncludeSystem \
  --suppress=unknownMacro --suppress=preprocessorErrorDirective \
  --suppress=uninitMemberVarNoCtor \
  -D BOARD_TRMNL_X -D CLOCK91_MODE=1 -D PARALLEL_EPD \
  -D CONFIG_IDF_TARGET_ESP32S3=1 -D PROGMEM= -D ARDUINO=100 \
  -I include -I lib/trmnl/include -I lib/trmnl_x/include -I lib/wificaptive/src \
  lib/trmnl/src/layout.cpp lib/trmnl/src/victron.cpp lib/trmnl/src/ruuvi.cpp \
  lib/trmnl/src/alarm.cpp lib/trmnl/src/timer.cpp lib/trmnl/src/format.cpp \
  lib/trmnl/src/forecast.cpp lib/trmnl/src/sunset.cpp lib/trmnl/src/fmi_parse.cpp \
  src/clock91.cpp src/render.cpp src/ble_scan.cpp src/fmi_fetch.cpp src/buzzer.cpp \
  "$@"
