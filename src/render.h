// Page rendering for the 540x960 portrait e-ink display.

#pragma once

#include <M5GFX.h>

#include "model.h"

struct AppStatus {
  bool wifiOk = false;
  bool timeOk = false;
  bool offline = false;       // last fetch failed, showing cache
  bool noWifiConfig = false;
  bool about = false;         // settings → about
  int battery = -1;
  uint32_t lastFetchOk = 0;   // epoch of last successful fetch
  uint32_t nextFetch = 0;     // epoch of next planned fetch
  int page = 0;
  char wifiSsid[33] = {0};
};

namespace render {

// Stable page ids (not footer indices — the tab bar is dynamic).
constexpr int kPageRegular = 0;
constexpr int kPageAnarchy = 1;
constexpr int kPageX = 2;
constexpr int kPageEvents = 3;
constexpr int kPageFest = 4;
constexpr int kPageSalmon = 5;
constexpr int kPageGear = 6;
constexpr int kPageSettings = 7;
constexpr int kW = 540, kH = 960;
constexpr int kHeaderH = 76;
constexpr int kFooterH = 56;

bool begin();                                   // canvas + fonts
void showStatus(const char* line1, const char* line2 = nullptr);
void drawPage(const Model& m, const AppStatus& st, bool quality = true);
void refreshHeader(const Model& m, const AppStatus& st);  // minute tick strip
// dump current page canvas as PGM P5 (8-bit gray) over Serial — dev tooling
bool dumpCanvas();
M5Canvas* canvas();  // full-screen page sprite (wifi onboarding reuses it)
int footerPageAt(int x, int y, const Model& m);  // page id, or -1
int settingsHit(int x, int y, bool about);  // 0 none, 1 wifi, 2 about, 3 back, 4 refresh
int neighborPage(const Model& m, int current, int dir);
int clampPage(const Model& m, int page);
int defaultPage(const Model& m);
bool festBattleActive(const Model& m);

}  // namespace render

// draws the little-buddy sleep screen onto a white canvas (used by power module)
bool powerDrawSleepHint(M5Canvas& c);
