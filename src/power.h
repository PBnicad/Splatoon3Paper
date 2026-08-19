// Battery / sleep helpers.

#pragma once

// -1 when unknown
int powerBatteryPercent();
// Draw the sleep hint on screen, then deep sleep until touch.
void powerEnterTouchSleep();
