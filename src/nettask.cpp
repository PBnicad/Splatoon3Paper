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

static void scheduleNextFetch(bool ok) {
  uint32_t now = nowEpoch();
  uint32_t next;
  if (ok) {
    struct tm lt;
    time_t t = now;
    localtime_r(&t, &lt);
    uint32_t dayStart = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
    next = dayStart + 3600 * (lt.tm_hour + 1) + 60 * kFetchAtMinute;
    if (next <= now) next += 3600;
  } else {
    next = now + kRetrySecs;
  }
  StateHold hold;
  sSt->nextFetch = next;
}

static bool fetchCompact() {
  String etagIn;
  {
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
    scheduleNextFetch(true);
    return true;
  }
  if (code != 200 || body.length() < 16) return false;

  static Model incoming;
  if (!modelParse(incoming, body.c_str(), body.length())) return false;
  cacheSaveCompact(body.c_str(), body.length(), newEtag.c_str());

  {
    StateHold hold;
    *sModel = incoming;
    *sHasModel = true;
    *sEtag = newEtag;
    sSt->offline = false;
    sSt->lastFetchOk = nowEpoch();
    imgQueue(*sModel);
  }
  scheduleNextFetch(true);
  return true;
}

static void netTask(void*) {
  Serial.printf("[nettask] start core=%d\n", xPortGetCoreID());
  uint32_t lastKeep = 0;
  for (;;) {
    if (sPaused) {
      sPauseAck = true;
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    sPauseAck = false;
    if (sCfg->wifiConfigured()) {
      if (!wifiConnected()) {
        bool ok = wifiConnect(sCfg->ssid(), sCfg->password(), 15000);
        {
          StateHold hold;
          sSt->wifiOk = ok;
          if (!ok) sSt->offline = true;
        }
        post(kNetEvtWifi);
        if (ok && !sSt->timeOk) {
          bool t = timeSyncNtp(12000);
          StateHold hold;
          sSt->timeOk = t;
          post(kNetEvtNtp);
        }
      } else if (!sSt->timeOk) {
        bool t = timeSyncNtp(8000);
        StateHold hold;
        sSt->timeOk = t;
        if (t) post(kNetEvtNtp);
      }

      uint32_t now = nowEpoch();
      bool due = false;
      {
        StateHold hold;
        due = sCfg->autoFetch() && (sForceFetch || now >= sSt->nextFetch);
      }
      if (due && wifiConnected()) {
        sForceFetch = false;
        static uint32_t lastAttempt = 0;
        if (now - lastAttempt >= 30 || lastAttempt == 0) {
          lastAttempt = now;
          bool ok = fetchCompact();
          if (!ok) {
            {
              StateHold hold;
              sSt->offline = true;
            }
            scheduleNextFetch(false);
            post(kNetEvtFetchFail);
          } else {
            post(kNetEvtFetchOk);
          }
        }
      }

      if (wifiConnected() && *sHasModel) {
        imgPump();
        if (imgJustFinished()) post(kNetEvtImg);
      }
    }

    uint32_t ms = millis();
    if (ms - lastKeep > 10000) {
      lastKeep = ms;
      wifiKeepAlive();
    }
    vTaskDelay(pdMS_TO_TICKS(40));
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
