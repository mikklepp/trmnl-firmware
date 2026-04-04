#pragma once

#include "layout.h"

// Initialize the renderer (call after display_init).
// Loads font pointers — must be called once before renderDrawList.
void render_init(void);

// Render a DrawList to the e-paper framebuffer.
// Call between allocBuffer() and writePlane()/refresh().
void renderDrawList(const DrawList& dl);

// Convenience: clear screen, render layout, and do a partial refresh.
void renderPartial(const DrawList& dl);

// Convenience: clear screen, render layout, and do a full refresh.
void renderFull(const DrawList& dl);

// Partial refresh of clock area only.
// Clears the clock rectangle, draws time digits, partial updates those rows.
void renderClockPartial(int hour, int minute);

// Partial refresh of timer countdown area (M:SS + progress bar).
// remaining/total in seconds. Redraws the timer zone (y=680→1404).
void renderTimerPartial(int remaining, int total);
