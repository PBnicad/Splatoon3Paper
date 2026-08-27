#include "nettask.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "cache.h"
#include "config.h"
#include "img.h"
#include "net.h"
#include "timekeeper.h"

static Config* sCfg = nullptr;
static Model* sModel = nullptr;
static bool* sHasModel = nullptr;
static AppStatus* sSt = nullptr;
static String* sEtag = nullptr;
static SemaphoreHandle_t sStateMx = nullptr;
static QueueHandle_t sEvtQ = nullptr;
static volatile bool sForceFetch = false;
static volatile bool sForceImgs = false;
static volatile bool sBusy = false;
static volatile bool sPaused = false;
static volatile bool sPauseAck = false;
static bool sStarted = false;

void stateLock() {
  if (sStateMx) xSemaphoreTake(sStateMx, portMAX_DELAY);
}
void stateUnlock() {
  if (sStateMx) xSemaphoreGive(sStateMx);
}

void netRequestFetch() { sForceFetch = true; }
void netRequestImgs() { sForceImgs = true; }
bool netBusy() { return sBusy || sForceFetch || sForceImgs || sPaused; }

void netPause(bool on) {
  sPaused = on;
  if (!on) {
    sPauseAck = false;
    return;
  }
  if (!sStarted) {
    sPauseAck = true;
    return;
  }
  uint32_t t0 = millis();
  while (!sPauseAck && millis() - t0 < 25000) delay(10);
}

bool netPollEvent(NetEvt& ev) {
  uint8_t b = 0;
  if (!sEvtQ || xQueueReceive(sEvtQ, &b, 0) != pdTRUE) return false;
  ev = (NetEvt)b;
  return true;
}

static void post(NetEvt ev) {
  if (!sEvtQ) return;
  uint8_t b = (uint8_t)ev;
  xQueueSend(sEvtQ, &b, 0);
}

// Backoff gates are kept in millis() (monotonic even before NTP), so a stuck
// 1970 clock can never turn retry sleeps into a busy reconnect loop.
static uint32_t sFailDelay = kRetrySecs;  // doubles per failure, reset on success
static uint32_t sRetryAfterMs = 0;        // millis() gate after a failed fetch
static uint32_t sNtpDelay = 60;           // NTP failure backoff
static uint32_t sNtpAfterMs = 0;          // millis() gate after a failed NTP sync

static bool gatePassed(uint32_t atMs) {
  if (atMs == 0) return true;  // no gate set (0 is also the post-success reset)
  return (int32_t)(millis() - atMs) >= 0;
}

static void scheduleNextFetch() {
  uint32_t now = nowEpoch();
  struct tm lt;
  time_t t = now;
  localtime_r(&t, &lt);
  uint32_t dayStart = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
  uint32_t next = dayStart + 3600 * (lt.tm_hour + 1) + 60 * kFetchAtMinute;
  if (next <= now) next += 3600;
  StateHold hold;
  sSt->nextFetch = next;
}

static void noteFetchOk() {
  sFailDelay = kRetrySecs;
  sRetryAfterMs = 0;
  scheduleNextFetch();
}

static void noteFetchFail() {
  sRetryAfterMs = millis() + sFailDelay * 1000UL;
  if (sFailDelay < 1800) sFailDelay *= 2;
  StateHold hold;
  sSt->nextFetch = nowEpoch() + sFailDelay;
}

static bool fetchCompact(bool force) {
  String etagIn;
  if (!force) {
    StateHold hold;
    etagIn = *sEtag;
  }
  String body;
  String newEtag;
  int code = httpsGet(kApiHost, kCompactPath, etagIn.c_str(), body, newEtag);
  Serial.printf("[nettask] fetch code=%d len=%u\n", code, (unsigned)body.length());
  if (code == 304) {
    {
      StateHold hold;
      sSt->offline = false;
      sSt->lastFetchOk = nowEpoch();
    }
    noteFetchOk();
    return true;
  }
  if (code != 200 || body.length() < 16) return false;

  static Model* incoming = nullptr;
  if (!incoming) {
    incoming = (Model*)ps_malloc(sizeof(Model));
    if (!incoming) incoming = (Model*)malloc(sizeof(Model));
    if (!incoming) {
      Serial.println("[nettask] no mem for model");
      return false;
    }
  }
  if (!modelParse(*incoming, body.c_str(), body.length())) return false;
  cacheSaveCompact(body.c_str(), body.length(), newEtag.c_str());

  {
    StateHold hold;
    *sModel = *incoming;
    *sHasModel = true;
    *sEtag = newEtag;
    sSt->offline = false;
    sSt->lastFetchOk = nowEpoch();
    if (timeValid()) sSt->timeOk = true;  // HTTP Date may have seeded the clock
    imgQueuePage(*sModel, sSt->page);
  }
  noteFetchOk();
  return true;
}

static bool ensureRadio() {
  if (wifiConnected()) return true;
  bool ok = wifiConnect(sCfg->ssid(), sCfg->password(), 15000);
  {
    StateHold hold;
    sSt->wifiOk = ok;
    if (!ok) sSt->offline = true;
  }
  post(kNetEvtWifi);
  return ok;
}

static void netTask(void*) {
  Serial.printf("[nettask] start core=%d\n", xPortGetCoreID());
  for (;;) {
    if (sPaused) {
      sBusy = false;
      sPauseAck = true;
      wifiRadioOff();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    sPauseAck = false;

    if (!sCfg->wifiConfigured()) {
      sBusy = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    uint32_t now = nowEpoch();
    bool fetchDue, needNtp;
    {
      StateHold hold;
      // Fetch runs before NTP: a successful response's HTTP Date header can
      // seed the clock, which often makes the NTP attempt unnecessary.
      fetchDue = sCfg->autoFetch() &&
                 (sForceFetch || (now >= sSt->nextFetch && gatePassed(sRetryAfterMs)));
      // timeValid() (not st.timeOk) drives NTP: any clock source counts, and
      // the millis gate keeps a blocked-NTP network from re-keying the radio
      // in a tight loop.
      needNtp = !timeValid() && gatePassed(sNtpAfterMs);
    }
    bool imgs = sForceImgs || imgPending();
    if (!fetchDue && !imgs && !needNtp && !sForceFetch) {
      sBusy = false;
      wifiRadioOff();
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    sBusy = true;
    if (!ensureRadio()) {
      sForceFetch = false;
      sForceImgs = false;
      noteFetchFail();
      post(kNetEvtFetchFail);
      wifiRadioOff();
      sBusy = false;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (fetchDue || sForceFetch) {
      bool forced = sForceFetch;
      sForceFetch = false;
      bool ok = fetchCompact(forced);
      if (!ok) {
        {
          StateHold hold;
          sSt->offline = true;
        }
        noteFetchFail();
        post(kNetEvtFetchFail);
      } else {
        post(kNetEvtFetchOk);
      }
    }

    if (needNtp && !timeValid()) {
      bool t = timeSyncNtp(12000);
      {
        StateHold hold;
        sSt->timeOk = t;
      }
      if (t) {
        sNtpDelay = 60;
        sNtpAfterMs = 0;
        post(kNetEvtNtp);
      } else {
        sNtpAfterMs = millis() + sNtpDelay * 1000UL;
        if (sNtpDelay < 1800) sNtpDelay *= 2;
      }
    }

    if (imgPump()) {
      if (imgJustFinished()) post(kNetEvtImg);
      continue;
    }
    sForceImgs = false;
    wifiRadioOff();
    sBusy = false;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void netTaskStart(Config& cfg, Model& model, bool& hasModel, AppStatus& st,
                  String& etag) {
  sCfg = &cfg;
  sModel = &model;
  sHasModel = &hasModel;
  sSt = &st;
  sEtag = &etag;
  if (!sStateMx) sStateMx = xSemaphoreCreateMutex();
  if (!sEvtQ) sEvtQ = xQueueCreate(12, sizeof(uint8_t));
  BaseType_t ok = xTaskCreatePinnedToCore(netTask, "net", 24576, nullptr, 1,
                                          nullptr, 0);
  sStarted = (ok == pdPASS);
  if (!sStarted) Serial.println("[nettask] FATAL create failed");
}
