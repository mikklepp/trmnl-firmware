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

// Prepare pPrevious buffer after deep sleep by rendering the previous time.
// Call once after wake, before renderClockUpdate(). Not needed while awake
// (e.g. timer loop) since pPrevious is maintained by prior updates.
void renderClockPrepare(int prev_hour, int prev_minute);

// Render new time into pCurrent and partial-update the clock rows.
void renderClockUpdate(int hour, int minute);

// Full-screen partial refresh from a DrawList (used for timer mode).
// Clears framebuffer, renders all commands, partial-updates entire display.
// Only pixels that differ from pPrevious actually flash on screen.
void renderTimerFrame(const DrawList& dl);
