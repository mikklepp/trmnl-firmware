#pragma once

#ifdef CLOCK91_MODE

// Initialize the Qwiic buzzer on I2C. Call after Wire.begin().
// Returns true if buzzer found at address 0x34.
bool buzzer_init(void);

// Timer completion: short beep pattern (beep-beep-beep)
void buzzer_timer(void);

// Alarm: longer insistent pattern
void buzzer_alarm(void);

// Single short beep (UI feedback)
void buzzer_beep(void);

#endif // CLOCK91_MODE
