#include "buzzer.h"

#ifdef CLOCK91_MODE

#include <SparkFun_Qwiic_Buzzer_Arduino_Library.h>
#include <Wire.h>
#include <trmnl_log.h>

static QwiicBuzzer buzzer;
static bool buzzer_ok = false;

bool buzzer_init(void) {
    if (buzzer.begin()) {
        buzzer_ok = true;
        Log_info("Buzzer: found at 0x34");
    } else {
        buzzer_ok = false;
        Log_info("Buzzer: not found (optional)");
    }
    return buzzer_ok;
}

static void beep(uint16_t freq, uint16_t duration_ms) {
    if (!buzzer_ok) return;
    buzzer.configureBuzzer(freq, duration_ms, SFE_QWIIC_BUZZER_VOLUME_MAX);
    buzzer.on();
    delay(duration_ms);
    buzzer.off();
}

void buzzer_beep(void) {
    beep(2000, 100);
}

void buzzer_timer(void) {
    // Three short beeps
    for (int i = 0; i < 3; i++) {
        beep(2500, 150);
        delay(100);
    }
}

void buzzer_alarm(void) {
    // Five longer beeps, rising pitch
    for (int i = 0; i < 5; i++) {
        beep(1500 + i * 300, 300);
        delay(150);
    }
}

#endif // CLOCK91_MODE
