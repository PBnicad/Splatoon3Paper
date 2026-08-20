// Battery / sleep helpers.

#pragma once

// -1 when unknown
int powerBatteryPercent();
// Buddy screen, radios off, light-sleep until the wheel is double-clicked.
void powerEnterSleep();
