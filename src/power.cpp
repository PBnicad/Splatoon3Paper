#include "power.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "img.h"
#include "net.h"

int powerBatteryPercent() {
  int level = M5.Power.getBatteryLevel();
  if (level >= 0 && level <= 100) return level;
  // voltage fallback (M5Paper: ~3500-4200mV)
  int mv = M5.Power.getBatteryVoltage();
  if (mv <= 0) return -1;
  if (mv >= 4150) return 100;
  if (mv <= 3450) return 0;
  return (mv - 3450) * 100 / (4150 - 3450);
}

void powerEnterTouchSleep() {
  if (wifiConnected()) imgPrefetchKey(kBuddyKey);
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5Canvas c(&M5.Display);
  c.setColorDepth(4);
  c.createSprite(M5.Display.width(), M5.Display.height());
  c.fillSprite(0);
  extern bool powerDrawSleepHint(M5Canvas& c);
  powerDrawSleepHint(c);
  c.pushSprite(0, 0);
  delay(600);  // let the panel settle
  M5.Power.deepSleep();  // sleep_no_timer + touch wakeup (M5Unified handles GT911)
}
