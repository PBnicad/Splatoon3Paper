// Battery / sleep helpers.

#pragma once

#include <cstdint>

// -1 when unknown
int powerBatteryPercent();
void displayWake();
void displayRest();  // IT8951 sleep; image stays on the panel
// Idle: light-sleep up to maxMs, wake on wheel / touch INT / timer.
void powerIdleSleep(uint32_t maxMs);
// Buddy screen, radios off, light-sleep until the wheel is double-clicked.
void powerEnterSleep();
