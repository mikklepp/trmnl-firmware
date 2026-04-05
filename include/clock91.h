#pragma once

#ifdef CLOCK91_MODE

// One-time initialization: timezone, render, buzzer, first full cycle.
// Called from bl_init() after common hardware init (display, preferences, IQS323).
void clock91_init(void);

// Main loop tick. Called from bl_process() every Arduino loop iteration.
// Handles light sleep (or delay in debug builds) internally.
void clock91_loop(void);

#endif // CLOCK91_MODE
