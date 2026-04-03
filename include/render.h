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
