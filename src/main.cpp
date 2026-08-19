/* M5Paper · 斯普拉遁3 日程墨水屏
 *
 * Fetches the device-tailored compact view from api.splatoon.icu (a
 * Cloudflare Worker that proxies + derives splatoon3.ink data) and renders
 * battle / salmon run / event / splatfest schedules on the 540x960 e-ink
 * panel. See README.md and worker/API.md.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <time.h>

#include "cache.h"
#include "config.h"
#include "font.h"
#include "model.h"
#include "net.h"
#include "power.h"
#include "render.h"
#include "timekeeper.h"

static Config cfg;
static Model model;
static bool hasModel = false;
static AppStatus st;
static String etag;
static uint32_t lastMinute = 0;

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

  Model fresh;
  if (!modelParse(fresh, body.c_str(), body.length())) return false;
  model = fresh;
  hasModel = true;
  etag = newEtag;
  cacheSaveCompact(body.c_str(), body.length(), newEtag.c_str());
  st.offline = false;
  st.lastFetchOk = nowEpoch();
  scheduleNextFetch(true);
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

static void goPage(int page) {
  if (page < 0) page = render::kPageCount - 1;
  if (page >= render::kPageCount) page = 0;
  st.page = page;
  if (hasModel) render::drawPage(model, st);
}

// ------------------------------------------------------------------ touch --

static void handleTap(int x, int y) {
  if (y >= render::kH - 60) {  // footer: page dots quick-jump / sleep
    int idx = (x - 10) / 22;
    if (idx >= 0 && idx < render::kPageCount) {
      goPage(idx);
      return;
    }
    if (x > render::kW - 140) {  // attribution corner → sleep
      powerEnterTouchSleep();
    }
    return;
  }
  if (y < render::kHeaderH && x > render::kW - 260) {
    // clock area → manual refresh
    st.nextFetch = 0;
    return;
  }
  if (st.page == 1 && y < 178) {  // P2 tab bar
    int ntabs = model.nModes;
    if (ntabs > 0) {
      st.p2Tab = x / (render::kW / ntabs);
      if (hasModel) render::drawPage(model, st);
    }
    return;
  }
  if (x < 90) goPage(st.page - 1);
  else if (x > render::kW - 90) goPage(st.page + 1);
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
      } else if (line == "sleep") {
        powerEnterTouchSleep();
      } else if (line.startsWith("page ")) {
        goPage(line.substring(5).toInt());
      } else if (line == "status") {
        Serial.printf("page=%d wifi=%d offline=%d hasModel=%d gen=%lu nf=%lu\n",
                      st.page, st.wifiOk, st.offline, hasModel,
                      (unsigned long)model.gen, (unsigned long)model.nf);
      } else {
        Serial.println("cmds: wifi SSID PASS | refetch | page N | sleep | status");
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
  } else {
    loadCacheIfEmpty();
    if (hasModel) render::drawPage(model, st, true);
    else render::showStatus("启动中…", ui::NoWifiLine2);
  }

  if (cfg.wifiConfigured()) {
    st.wifiOk = wifiConnect(cfg.ssid(), cfg.password(), 15000);
  } else {
    st.noWifiConfig = true;
    Serial.println("[app] no wifi configured; send: wifi SSID PASSWORD");
  }
  if (st.wifiOk) {
    st.timeOk = timeSyncNtp(12000);
    st.battery = powerBatteryPercent();
    if (fetchCompact()) {
      render::drawPage(model, st, true);
    } else {
      st.offline = true;
      loadCacheIfEmpty();
      if (hasModel) render::drawPage(model, st, true);
      else render::showStatus("网络失败", "稍后自动重试");
      scheduleNextFetch(false);
    }
  } else if (hasModel) {
    st.offline = true;
    scheduleNextFetch(false);
  }
}

void loop() {
  M5.update();
  handleSerial();

  auto t = M5.Touch.getDetail();
  if (t.wasClicked()) handleTap(t.x, t.y);

  uint32_t now = nowEpoch();
  uint32_t minute = now / 60;
  if (minute != lastMinute && timeValid()) {
    lastMinute = minute;
    if (hasModel) render::refreshHeader(model, st);
    st.battery = powerBatteryPercent();
  }

  if (cfg.wifiConfigured() && now >= st.nextFetch) {
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
      if (hasModel) render::drawPage(model, st, true);
    }
  }

  wifiKeepAlive();
  delay(50);
}
