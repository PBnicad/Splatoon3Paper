// Device-side model of /api/v1/compact (see worker/API.md for the contract).

#pragma once

#include <cstdint>
#include <cstddef>

struct Slot {
  uint32_t st = 0, et = 0;
  char rn[28] = {0};   // rule name (zh)
  char s1[40] = {0};   // stage 1
  char s2[40] = {0};   // stage 2
  char si1[72] = {0};  // image keys s:{hash}_{0|1}
  char si2[72] = {0};
};

struct ModeSlots {
  const char* label = nullptr;  // points into config.h ui:: literals
  bool hasA = false;
  Slot a;
  Slot u[12];
  int nu = 0;
};

struct Period { uint32_t st, et; };

struct EventItem {
  uint32_t st = 0, et = 0;
  char n[48] = {0};
  char d[200] = {0};
  char r[260] = {0};
  char rn[28] = {0};
  char s1[40] = {0}, s2[40] = {0};
  char si1[72] = {0}, si2[72] = {0};
  Period p[10];
  int np = 0;
};

struct Shift {
  uint32_t st = 0, et = 0;
  char stage[40] = {0};
  char si[72] = {0};
  char boss[24] = {0};   // empty when none
  char w[4][32] = {0};
  char wi[4][72] = {0};
  bool big = false, mys = false, gmys = false;
};

struct Team {
  char n[28] = {0};
  uint8_t r = 0, g = 0, b = 0;
  bool win = false;
  bool hasVr = false, hasHr = false, hasOcr = false, hasCcr = false, hasTcr = false;
  int16_t vr = 0, hr = 0, ocr = 0, ccr = 0, tcr = 0;  // percent ×100
};

struct FestInfo {
  bool present = false;
  char state[16] = {0};
  char title[72] = {0};
  uint32_t st = 0, et = 0, mt = 0;
  int nTeams = 0;
  Team teams[3];
  char tri[2][40] = {0};
  int nTri = 0;
};

struct FestHist {
  bool present = false;
  char title[72] = {0};
  uint32_t st = 0, et = 0;
  int nTeams = 0;
  Team teams[3];
};

struct GearItem {
  char n[44] = {0};
  int32_t p = 0;
  uint32_t et = 0;
  char pn[26] = {0};
  char img[72] = {0};
};

struct Model {
  uint32_t gen = 0, nf = 0;
  ModeSlots modes[6];       // regular, series, open, x, festOpen, festPro
  int nModes = 0;
  FestInfo fest;
  FestHist festNext;
  FestHist festRecent[2];
  int nFestRecent = 0;
  EventItem events[4];
  int nEvents = 0;
  Shift shifts[8];
  int nShifts = 0;
  Shift eggstra[4];
  int nEggstra = 0;
  GearItem gear[6];
  int nGear = 0;
  char monthly[44] = {0};
  bool hasMonthly = false;

  const ModeSlots* findMode(const char* key) const;
};

// Parse compact JSON (buffer may be in PSRAM). Returns false on hard error.
bool modelParse(Model& m, const char* json, size_t len);
