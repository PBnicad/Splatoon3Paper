// Page rendering for the 540x960 portrait e-ink display.

#pragma once

#include <M5GFX.h>

#include "model.h"

struct AppStatus {
  bool wifiOk = false;
  bool timeOk = false;
  bool offline = false;       // last fetch failed, showing cache
  bool noWifiConfig = false;
  int battery = -1;
  uint32_t lastFetchOk = 0;   // epoch of last successful fetch
  uint32_t nextFetch = 0;     // epoch of next planned fetch
  int page = 0;
  int p2Tab = 0;              // P2 mode tab selection
};

namespace render {

constexpr int kPageCount = 5;
constexpr int kW = 540, kH = 960;
constexpr int kHeaderH = 110;

bool begin();                                   // canvas + fonts
void showStatus(const char* line1, const char* line2 = nullptr);
void drawPage(const Model& m, const AppStatus& st, bool quality = false);
void refreshHeader(const Model& m, const AppStatus& st);  // minute tick strip

}  // namespace render

// draws the touch-wake hint onto a black canvas (used by power module)
bool powerDrawSleepHint(M5Canvas& c);
