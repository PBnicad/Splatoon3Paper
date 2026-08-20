/* M5Paper · 斯普拉遁3 日程墨水屏
 *
 * Fetches the device-tailored compact view from api.splatoon.icu (a
 * Cloudflare Worker that proxies + derives splatoon3.ink data) and renders
 * battle / salmon run / event / splatfest schedules on the 540x960 e-ink
 * panel. See README.md and worker/API.md.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>

#include "cache.h"
#include "config.h"
#include "font.h"
#include "model.h"
#include "net.h"
#include "nettask.h"
#include "power.h"
#include "render.h"
#include "timekeeper.h"
#include "wifiui.h"

static Config cfg;
static Model model;
static bool hasModel = false;
static AppStatus st;
static String etag;
static uint32_t lastMinute = 0;
static uint32_t gBodyUntil = 0;  // redraw maps when the current 2h slot ends

static int readWheelDir() {
  bool up = M5.BtnA.wasPressed();
  bool down = M5.BtnC.wasPressed();
  if (up && !down) return -1;
  if (down && !up) return 1;
  return 0;
}

static void loadCacheIfEmpty() {
  if (hasModel) return;
  size_t len = 0;
  char* buf = cacheLoadCompact(len);
  if (!buf) return;
  uint32_t ts = 0;
  String metaEtag;
  cacheLoadMeta(ts, metaEtag);
  if (modelParse(model, buf, len)) {
    hasModel = true;
    etag = metaEtag;
    st.lastFetchOk = ts;
    Serial.println("[app] loaded cached model");
  }
  free(buf);
}

static void paint(bool quality = true) {
  if (cfg.wifiConfigured()) {
    snprintf(st.wifiSsid, sizeof(st.wifiSsid), "%s", cfg.ssid());
    st.noWifiConfig = false;
  } else {
    st.wifiSsid[0] = 0;
    st.noWifiConfig = true;
  }
  StateHold hold;
  st.page = render::clampPage(model, st.page);
  render::drawPage(model, st, quality);
  gBodyUntil = model.nextChangeAt(nowEpoch());
}

static void goPage(int page) {
  st.page = page;
  st.about = false;
  paint(true);
}

static void goNeighbor(int dir) {
  if (!dir) return;
  int next;
  {
    StateHold hold;
    next = render::neighborPage(model, st.page, dir);
  }
  goPage(next);
}

// ------------------------------------------------------------------ touch --

static void handleTap(int x, int y) {
  int tab;
  {
    StateHold hold;
    tab = render::footerPageAt(x, y, model);
  }
  if (tab >= 0) {
    goPage(tab);
    return;
  }
  if (y < render::kHeaderH && x > render::kW - 90) {
    netPause(true);
    if (wifiuiRun(cfg)) {
      Serial.println("[app] wifi saved from UI, rebooting");
      delay(300);
      ESP.restart();
    }
    netPause(false);
    paint();
    return;
  }
  if (st.page == render::kPageSettings) {
    int hit = render::settingsHit(x, y, st.about);
    if (hit == 1) {
      netPause(true);
      if (wifiuiRun(cfg)) {
        Serial.println("[app] wifi saved from UI, rebooting");
        delay(300);
        ESP.restart();
      }
      netPause(false);
      paint();
    } else if (hit == 2) {
      st.about = true;
      paint();
    } else if (hit == 3) {
      st.about = false;
      paint();
    }
    return;
  }
  int dir = 0;
  if (x < 90) dir = -1;
  else if (x > render::kW - 90) dir = 1;
  if (dir) goNeighbor(dir);
}

// ----------------------------------------------------------------- serial --

static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (!line.length()) continue;
      line.trim();
      Serial.printf("> %s\n", line.c_str());
      if (line.startsWith("wifi ")) {
        int sp = line.indexOf(' ', 5);
        if (sp > 5) {
          cfg.setWifi(line.substring(5, sp).c_str(), line.substring(sp + 1).c_str());
          Serial.println("[app] wifi saved, rebooting");
          delay(300);
          ESP.restart();
        } else {
          Serial.println("usage: wifi SSID PASSWORD");
        }
      } else if (line == "refetch") {
        netRequestFetch();
      } else if (line.startsWith("autofetch ")) {
        bool on = line.substring(10) == "1";
        cfg.setAutoFetch(on);
        Serial.printf("[app] autofetch=%d\n", on);
      } else if (line == "shot") {
        Serial.println("[app] dump canvas start");
        render::dumpCanvas();
        Serial.println("[app] dump canvas done");
      } else if (line == "dumpcache") {
        FsHold hold;
        File f = LittleFS.open("/compact.json", "r");
        if (f) {
          Serial.printf("[app] cache size=%u\n", (unsigned)f.size());
          while (f.available()) Serial.write(f.read());
          f.close();
          Serial.println();
          Serial.println("[app] dumpcache end");
        } else {
          Serial.println("[app] no cache");
        }
      } else if (line == "sleep") {
        powerEnterSleep();
        paint();
      } else if (line == "sleepview") {
        M5Canvas* c = render::canvas();
        if (c) {
          powerDrawSleepHint(*c);
          M5.Display.setEpdMode(epd_mode_t::epd_quality);
          c->pushSprite(0, 0);
        }
      } else if (line.startsWith("page ")) {
        goPage(line.substring(5).toInt());
      } else if (line == "about") {
        st.page = render::kPageSettings;
        st.about = true;
        paint();
      } else if (line == "guide") {
        netPause(true);
        wifiuiRun(cfg, true);
        netPause(false);
        paint();
      } else if (line == "factoryreset") {
        netPause(true);
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        cacheClearUserData();
        cfg.factoryReset();
        Serial.println("[app] factory reset, rebooting");
        delay(400);
        ESP.restart();
      } else if (line == "status") {
        StateHold hold;
        Serial.printf("page=%d wifi=%d offline=%d hasModel=%d gen=%lu nf=%lu modes=%d events=%d shifts=%d egg=%d gear=%d\n",
                      st.page, st.wifiOk, st.offline, hasModel,
                      (unsigned long)model.gen, (unsigned long)model.nf,
                      model.nModes, model.nEvents, model.nShifts,
                      model.nEggstra, model.nGear);
      } else {
        Serial.println("cmds: wifi SSID PASS | refetch | page N | sleep | sleepview | status | autofetch 0/1 | shot | dumpcache | factoryreset");
      }
      line = "";
    } else if (line.length() < 128) {
      line += c;
    }
  }
}

// -------------------------------------------------------------------- app --

void setup() {
  auto m5cfg = M5.config();
  m5cfg.serial_baudrate = 115200;
  M5.begin(m5cfg);
  M5.Power.begin();
  Serial.println("\n[app] splatoon3 m5paper boot");
  Serial.printf("[app] heap=%u intern=%u psram=%u loopstack-hwm=%u model=%u\n",
                ESP.getFreeHeap(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)sizeof(Model));

  netInit();
  if (!cacheBegin()) Serial.println("[app] WARN littlefs mount failed");
  fontSetup();
  cfg.begin();
  timeSeedFromRtc();
  st.battery = powerBatteryPercent();

  if (!render::begin()) {
    Serial.println("[app] FATAL canvas alloc failed");
    while (true) delay(1000);
  }
  if (!font24.valid()) {
    render::showStatus("字库缺失", "运行: pio run -t uploadfs");
    return;
  }

  if (!cfg.wifiConfigured() && !cfg.onboarded()) {
    bool saved = wifiuiRun(cfg, true);
    cfg.setOnboarded(true);
    if (saved) {
      delay(300);
      ESP.restart();
    }
  }

  loadCacheIfEmpty();
  if (!cfg.wifiConfigured()) st.noWifiConfig = true;
  paint();
  if (cfg.wifiConfigured()) netRequestFetch();
  netTaskStart(cfg, model, hasModel, st, etag);
  if (!cfg.wifiConfigured())
    Serial.println("[app] no wifi; open 设置 or send: wifi SSID PASSWORD");
}

void loop() {
  M5.update();
  handleSerial();

  if (M5.BtnB.wasPressed()) {
    static uint32_t lastClick = 0;
    uint32_t t = millis();
    if (lastClick && t - lastClick < 500) {
      lastClick = 0;
      Serial.println("[app] wheel double-click → sleep");
      powerEnterSleep();
      paint();
    } else {
      lastClick = t;
    }
  }

  auto t = M5.Touch.getDetail();
  if (t.wasClicked()) handleTap(t.x, t.y);
  else goNeighbor(readWheelDir());

  uint32_t now = nowEpoch();
  uint32_t minute = now / 60;
  if (minute != lastMinute && timeValid()) {
    lastMinute = minute;
    st.battery = powerBatteryPercent();
    if (gBodyUntil && now >= gBodyUntil) {
      paint(true);  // 18:00-20:00 → 20:00-22:00 without waiting for fetch
    } else {
      StateHold hold;
      render::refreshHeader(model, st);
    }
  }

  NetEvt ev;
  bool full = false, fast = false, head = false;
  while (netPollEvent(ev)) {
    if (ev == kNetEvtFetchOk || ev == kNetEvtFetchFail) full = true;
    else if (ev == kNetEvtImg) fast = true;
    else if (ev == kNetEvtWifi || ev == kNetEvtNtp) head = true;
  }
  if (full) paint(true);
  else if (fast) paint(false);
  else if (head) {
    StateHold hold;
    render::refreshHeader(model, st);
  }

  delay(20);
}
