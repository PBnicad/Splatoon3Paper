#include "power.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "nettask.h"

static constexpr gpio_num_t kBtnBPin = GPIO_NUM_38;  // M5Paper wheel press
static constexpr uint32_t kDblMs = 500;

int powerBatteryPercent() {
  int level = M5.Power.getBatteryLevel();
  if (level >= 0 && level <= 100) return level;
  int mv = M5.Power.getBatteryVoltage();
  if (mv <= 0) return -1;
  if (mv >= 4150) return 100;
  if (mv <= 3450) return 0;
  return (mv - 3450) * 100 / (4150 - 3450);
}

static bool btnBDown() { return digitalRead(kBtnBPin) == LOW; }

static void waitBtnUp() {
  while (btnBDown()) delay(10);
  delay(30);
}

// True if a second press arrives within windowMs after the first (already down
// or just seen). Ignores the touch panel entirely.
static bool waitSecondClick(uint32_t windowMs) {
  uint32_t t0 = millis();
  waitBtnUp();
  while (millis() - t0 < windowMs) {
    if (btnBDown()) {
      waitBtnUp();
      return true;
    }
    delay(10);
  }
  return false;
}

void powerEnterSleep() {
  netPause(true);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
#ifdef CONFIG_BT_ENABLED
  btStop();
#endif

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5Canvas c(&M5.Display);
  c.setColorDepth(4);
  c.createSprite(M5.Display.width(), M5.Display.height());
  c.fillSprite(15);
  extern bool powerDrawSleepHint(M5Canvas& c);
  powerDrawSleepHint(c);
  c.pushSprite(0, 0);
  c.deleteSprite();
  M5.Display.waitDisplay();

  // The double-click that entered sleep must not count as a wake.
  pinMode(kBtnBPin, INPUT);
  waitBtnUp();
  delay(350);

  rtc_gpio_init(kBtnBPin);
  rtc_gpio_set_direction(kBtnBPin, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(kBtnBPin);
  rtc_gpio_pulldown_dis(kBtnBPin);
  esp_sleep_enable_ext0_wakeup(kBtnBPin, 0);

  Serial.println("[power] sleep: double-click wheel to wake");
  Serial.flush();

  for (;;) {
    waitBtnUp();
    esp_light_sleep_start();
    if (waitSecondClick(kDblMs)) break;
  }

  rtc_gpio_deinit(kBtnBPin);
  pinMode(kBtnBPin, INPUT);
  for (int i = 0; i < 4; ++i) {
    M5.update();
    delay(20);
  }

  netPause(false);
  Serial.println("[power] woke");
}
