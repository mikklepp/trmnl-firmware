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

// Render new time into pCurrent and partial-update the clock rows.
// Redraws the 88:88 ghost behind the digits; pPrevious must already match the
// panel (renderFull, or a prior tick). Light sleep preserves PSRAM, so no
// reconstruction step is needed after wake.
void renderClockUpdate(int hour, int minute);

// Timer mode rendering (DrawList + coffee cup bitmap at given steam frame).
// Full: greyscale wipe, used for initial transition into timer mode.
// Frame: partial update, only changed pixels flash. Used for 1s ticks.
void renderTimerFull(const DrawList& dl, int cup_frame);
void renderTimerFrame(const DrawList& dl, int cup_frame);
