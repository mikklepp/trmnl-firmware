#pragma once

#ifdef CLOCK91_MODE

// Run one clock91 wake cycle:
//   1. Connect WiFi
//   2. Sync time via NTP (if needed)
//   3. Build DisplayState from current time
//   4. Render to e-paper
//
// Called from bl_init() after common hardware init.
// Returns to bl_init() which handles sleep.
void clock91_cycle(void);

#endif // CLOCK91_MODE
