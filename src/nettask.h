// Background FreeRTOS task: Wi-Fi, NTP, compact fetch, image downloads.
// The Arduino loop() stays the UI task (touch / encoder / paint).

#pragma once

#include <WString.h>

#include "config.h"
#include "model.h"
#include "render.h"

void netTaskStart(Config& cfg, Model& model, bool& hasModel, AppStatus& st,
                  String& etag);

// Hold while reading/writing the shared Model (and related AppStatus flags).
void stateLock();
void stateUnlock();
struct StateHold {
  StateHold() { stateLock(); }
  ~StateHold() { stateUnlock(); }
};

void netRequestFetch();  // UI: force a compact refresh
void netPause(bool on);  // UI: stop net work around Wi-Fi setup

enum NetEvt : uint8_t {
  kNetEvtWifi = 1,
  kNetEvtFetchOk = 2,
  kNetEvtFetchFail = 3,
  kNetEvtImg = 4,
  kNetEvtNtp = 5,
};
// Non-blocking. Returns true and fills ev when the net task posted something.
bool netPollEvent(NetEvt& ev);
