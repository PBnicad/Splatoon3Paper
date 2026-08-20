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
#include <time.h>

#include "cache.h"
#include "config.h"
#include "font.h"
#include "img.h"
#include "model.h"
#include "net.h"
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
static int gTapX = -1, gTapY = -1;
static int gWheelDir = 0;  // -1 = BtnA (up / prev tab), +1 = BtnC (down / next)

static int readWheelDir() {
  bool up = M5.BtnA.wasPressed();
  bool down = M5.BtnC.wasPressed();
  if (up && !down) return -1;
  if (down && !up) return 1;
  return 0;
}

static bool onNetYield() {
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.wasClicked()) {
    gTapX = t.x;
    gTapY = t.y;
    return true;
  }
  int d = readWheelDir();
  if (d) {
    gWheelDir = d;
    return true;
  }
  return false;
}

static void scheduleNextFetch(bool ok) {
  uint32_t now = nowEpoch();
  if (ok) {
    // next hourly refresh shortly after splatoon3.ink publishes (~:10-:20)
    struct tm lt;
    time_t t = now;
    localtime_r(&t, &lt);
    uint32_t dayStart = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
    uint32_t next = dayStart + 3600 * (lt.tm_hour + 1) + 60 * kFetchAtMinute;
    if (next <= now) next += 3600;
    st.nextFetch = next;
  } else {
    st.nextFetch = now + kRetrySecs;
  }
}

static bool fetchCompact() {
  String body;
  String newEtag;
  int code = httpsGet(kApiHost, kCompactPath, etag.c_str(), body, newEtag);
  Serial.printf("[app] fetch code=%d len=%u\n", code, (unsigned)body.length());
  if (code == 304) {
    st.offline = false;
    st.lastFetchOk = nowEpoch();
    scheduleNextFetch(true);
    return true;  // unchanged
  }
  if (code != 200 || body.length() < 16) return false;

  // Model is ~16KB; keep the scratch copy in BSS so we don't blow the loop stack
  // (and so a failed parse cannot clobber the on-screen model).
  static Model incoming;
  if (!modelParse(incoming, body.c_str(), body.length())) return false;
  model = incoming;
  hasModel = true;
  etag = newEtag;
  cacheSaveCompact(body.c_str(), body.length(), newEtag.c_str());
  st.offline = false;
  st.lastFetchOk = nowEpoch();
  scheduleNextFetch(true);
  imgQueue(model);
  return true;
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
  st.page = render::clampPage(model, st.page);
  render::drawPage(model, st, quality);
}

static void goPage(int page) {
  st.page = render::clampPage(model, page);
  st.about = false;
  paint(true);
}

static void goNeighbor(int dir) {
  if (!dir) return;
  goPage(render::neighborPage(model, st.page, dir));
}

// ------------------------------------------------------------------ touch --

static void handleTap(int x, int y) {
  int tab = render::footerPageAt(x, y, model);
  if (tab >= 0) {
    goPage(tab);
    return;
  }
  if (y < render::kHeaderH && x > render::kW - 90) {
    if (wifiuiRun(cfg)) {
      Serial.println("[app] wifi saved from UI, rebooting");
      delay(300);
      ESP.restart();
    }
    paint();
    return;
  }
  if (st.page == render::kPageSettings) {
    int hit = render::settingsHit(x, y, st.about);
    if (hit == 1) {
      if (wifiuiRun(cfg)) {
        Serial.println("[app] wifi saved from UI, rebooting");
        delay(300);
        ESP.restart();
      }
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
  if (x < 90) goPage(render::neighborPage(model, st.page, -1));
  else if (x > render::kW - 90) goPage(render::neighborPage(model, st.page, 1));
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
        st.nextFetch = 0;
      } else if (line.startsWith("autofetch ")) {
        bool on = line.substring(10) == "1";
        cfg.setAutoFetch(on);
        Serial.printf("[app] autofetch=%d\n", on);
      } else if (line == "shot") {
        Serial.println("[app] dump canvas start");
        render::dumpCanvas();
        Serial.println("[app] dump canvas done");
      } else if (line == "dumpcache") {
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
        powerEnterTouchSleep();
      } else if (line.startsWith("page ")) {
        goPage(line.substring(5).toInt());
      } else if (line == "about") {
        st.page = render::kPageSettings;
        st.about = true;
        paint();
      } else if (line == "guide") {
        wifiuiRun(cfg, true);
        paint();
      } else if (line == "status") {
        Serial.printf("page=%d wifi=%d offline=%d hasModel=%d gen=%lu nf=%lu modes=%d events=%d shifts=%d egg=%d gear=%d\n",
                      st.page, st.wifiOk, st.offline, hasModel,
                      (unsigned long)model.gen, (unsigned long)model.nf,
                      model.nModes, model.nEvents, model.nShifts,
                      model.nEggstra, model.nGear);
      } else {
        Serial.println("cmds: wifi SSID PASS | refetch | page N | sleep | status | autofetch 0/1 | shot | dumpcache");
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
  netSetYield(onNetYield);
  Serial.println("\n[app] splatoon3 m5paper boot");
  Serial.printf("[app] heap=%u free, psram=%u free, loopstack-hwm=%u model=%u\n",
                ESP.getFreeHeap(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)sizeof(Model));

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
  if (!cfg.wifiConfigured()) {
    st.noWifiConfig = true;
    paint();
    Serial.println("[app] no wifi; open 设置 or send: wifi SSID PASSWORD");
    return;
  }

  paint();
  st.wifiOk = wifiConnect(cfg.ssid(), cfg.password(), 15000);
  if (st.wifiOk && cfg.autoFetch()) {
    st.timeOk = timeSyncNtp(12000);
    st.battery = powerBatteryPercent();
    if (fetchCompact()) {
      paint();
    } else {
      st.offline = true;
      loadCacheIfEmpty();
      paint();
      scheduleNextFetch(false);
    }
  } else {
    if (!st.wifiOk) st.offline = true;
    paint();
    if (hasModel) scheduleNextFetch(false);
  }
}

void loop() {
  M5.update();
  handleSerial();

  if (gTapX >= 0) {
    int x = gTapX, y = gTapY;
    gTapX = gTapY = -1;
    handleTap(x, y);
  } else if (gWheelDir != 0) {
    int d = gWheelDir;
    gWheelDir = 0;
    goNeighbor(d);
  } else {
    auto t = M5.Touch.getDetail();
    if (t.wasClicked()) handleTap(t.x, t.y);
    else goNeighbor(readWheelDir());
  }

  uint32_t now = nowEpoch();
  uint32_t minute = now / 60;
  if (minute != lastMinute && timeValid()) {
    lastMinute = minute;
    render::refreshHeader(model, st);
    st.battery = powerBatteryPercent();
  }

  if (cfg.wifiConfigured() && cfg.autoFetch() && now >= st.nextFetch) {
    static uint32_t lastAttempt = 0;
    if (now - lastAttempt >= 30) {  // simple in-flight guard
      lastAttempt = now;
      if (!st.wifiOk) st.wifiOk = wifiConnect(cfg.ssid(), cfg.password(), 12000);
      bool ok = st.wifiOk && fetchCompact();
      if (!ok) {
        st.offline = true;
        scheduleNextFetch(false);
      } else if (!st.timeOk) {
        st.timeOk = timeSyncNtp(8000);
      }
      paint(true);
    }
  }

  if (hasModel && st.wifiOk && gTapX < 0 && gWheelDir == 0) {
    imgPump();
    if (gTapX >= 0) {
      int x = gTapX, y = gTapY;
      gTapX = gTapY = -1;
      handleTap(x, y);
    } else if (gWheelDir != 0) {
      int d = gWheelDir;
      gWheelDir = 0;
      goNeighbor(d);
    } else if (imgJustFinished()) {
      paint(false);  // image arrivals: fast refresh, don't block the tab bar
    }
  }

  wifiKeepAlive();
  delay(20);
}
