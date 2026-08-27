#include "render.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <string.h>

#include "config.h"
#include "font.h"
#include "img.h"
#include "power.h"
#include "timekeeper.h"

namespace render {

static M5Canvas page(&M5.Display);
static M5Canvas header(&M5.Display);
static int fastPushes = 0;

// 4bpp grayscale palette
enum : uint8_t {
  C_BLACK = 0,
  C_DARK = 3,
  C_GRAY = 6,
  C_MID = 9,
  C_LIGHT = 12,
  C_WHITE = 15,
};

// SNF1 ink box (max yoff+h). Latin descenders on font24 reach 35, font40 58.
constexpr int kInk24 = 36;
constexpr int kInk40 = 58;
constexpr int kLine24 = kInk24 + 6;

static void pushFull(bool quality) {
  // Page turns and data redraws use GC16 full-frame refresh to clear ghosts.
  // Minute header ticks keep epd_fastest via refreshHeader().
  displayWake();
  M5.Display.setEpdMode(quality ? epd_mode_t::epd_quality : epd_mode_t::epd_fastest);
  page.pushSprite(0, 0);
  if (quality) {
    fastPushes = 0;
  } else if (++fastPushes >= 10) {
    fastPushes = 0;
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    page.pushSprite(0, 0);
  }
  displayRest();
}

bool begin() {
  if (M5.Display.width() > M5.Display.height()) {
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  }
  page.setColorDepth(4);
  header.setColorDepth(4);
  page.setPsram(true);
  header.setPsram(true);
  bool ok = page.createSprite(kW, kH) != nullptr;
  header.createSprite(kW, kHeaderH);
  return ok;
}

M5Canvas* canvas() { return &page; }

void showStatus(const char* line1, const char* line2) {
  page.fillSprite(C_WHITE);
  font40.draw(&page, 24, 380, line1, C_BLACK, C_WHITE);
  if (line2) font24.draw(&page, 24, 380 + kInk40 + 16, line2, C_GRAY, C_WHITE);
  pushFull(true);
}

// ---------------------------------------------------------------- helpers --

static void drawBattery(M5Canvas* c, int x, int y, int pct) {
  c->drawRect(x, y, 26, 14, C_BLACK);
  c->fillRect(x + 26, y + 4, 3, 6, C_BLACK);
  if (pct >= 0) {
    int w = 22 * pct / 100;
    c->fillRect(x + 2, y + 2, w, 10, C_BLACK);
  }
}

static void drawWifi(M5Canvas* c, int x, int y, bool ok, bool offline) {
  c->fillRect(x + 10, y + 10, 4, 4, C_BLACK);
  if (ok || offline) c->fillRect(x + 5, y + 5, 4, 4, C_BLACK);
  if (ok) c->fillRect(x, y, 4, 4, C_BLACK);
  if (offline) {
    for (int i = 0; i < 14; ++i) c->drawPixel(x + i, y + 13 - i, C_BLACK);
  }
}

static void drawText(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
  font24.draw(&page, x, y, s, fg, bg);
}

// ---------------------------------------------------------------- header --

static void drawHeaderInto(M5Canvas* c, const Model& m, const AppStatus& st) {
  (void)m;
  c->fillRect(0, 0, kW, kHeaderH, C_WHITE);
  if (timeValid()) {
    char date[32], clock[8];
    uint32_t t = nowEpoch();
    fmtDateWeek(t, date, sizeof(date));
    fmtClock(t, clock, sizeof(clock));
    font24.draw(c, 16, 2, date, C_DARK, C_WHITE);
    font24.draw(c, 16, 2 + kInk24 - 4, clock, C_BLACK, C_WHITE);
  }
  const int iconY = (kHeaderH - 14) / 2;
  int rx = kW - 16;
  drawBattery(c, rx - 29, iconY, st.battery);
  rx -= 44;
  drawWifi(c, rx - 16, iconY, st.wifiOk, st.offline);
  rx -= 28;
  if (st.offline || st.noWifiConfig) {
    const char* tag = st.noWifiConfig ? ui::NoWifi : ui::Offline;
    int tw = font24.textWidth(tag);
    int ty = (kHeaderH - kInk24) / 2;
    if (ty < 2) ty = 2;
    font24.draw(c, rx - tw, ty, tag, C_BLACK, C_WHITE);
  }
  c->drawFastHLine(0, kHeaderH - 2, kW, C_MID);
}

void refreshHeader(const Model& m, const AppStatus& st) {
  drawHeaderInto(&header, m, st);
  drawHeaderInto(&page, m, st);
  displayWake();
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  header.pushSprite(0, 0);
  displayRest();
}

// ------------------------------------------------------------- page 1 cards

static void drawImgOrBox(int x, int y, int w, int h, const char* key) {
  page.drawRect(x, y, w, h, C_LIGHT);
  if (!key || !key[0] || !imgDrawFit(&page, x + 1, y + 1, w - 2, h - 2, key)) {
    page.fillRect(x + 1, y + 1, w - 2, h - 2, C_LIGHT);
  }
}

constexpr int kTitleY = kHeaderH + 8;
constexpr int kTitleLine = kTitleY + kInk24 + 8;
constexpr int kContentY = kTitleLine + 10;
constexpr int kMapW = 248;
constexpr int kMapHNative = 124;
constexpr int kMapGap = 8;
constexpr int kSecH = kInk24 + 10;  // section label + gap before list

static int slotCardH(int imgH) {
  // pad + time/rule + gap + maps + gap + names + pad
  return 8 + kInk24 + 10 + imgH + 8 + kInk24 + 10;
}

static int fitSlots(int y0, int imgH, int extraOverhead) {
  int avail = kH - kFooterH - y0 - extraOverhead;
  int h = slotCardH(imgH);
  if (h <= 0) return 1;
  int n = avail / h;
  return n < 1 ? 1 : n;
}

static void drawPageTitle(const char* t) {
  font24.draw(&page, 24, kTitleY, t, C_BLACK, C_WHITE);
  page.drawFastHLine(0, kTitleLine, kW, C_MID);
}

static int drawFestTeams(int y, const Team* teams, int n);

static const char* weaponShown(const Shift& s, int i) {
  if (s.mys && strcmp(s.w[i], "随机") == 0)
    return s.gmys ? ui::GrizzcoRandom : ui::RandomWeapon;
  return s.w[i];
}

bool festBattleActive(const Model& m) {
  if (!m.fest.present) return false;
  if (!strcmp(m.fest.state, "FIRST_HALF") || !strcmp(m.fest.state, "SECOND_HALF"))
    return true;
  uint32_t now = nowEpoch();
  return !m.fest.state[0] && m.fest.st && m.fest.et && now >= m.fest.st && now < m.fest.et;
}

static int fillTabs(const Model& m, int* ids, const char** labels, int cap) {
  int n = 0;
  auto push = [&](int id, const char* lab) {
    if (n >= cap) return;
    if (ids) ids[n] = id;
    if (labels) labels[n] = lab;
    ++n;
  };
  if (festBattleActive(m)) {
    push(kPageFest, ui::NavFest);
  } else {
    push(kPageRegular, ui::NavRegular);
    push(kPageAnarchy, ui::NavAnarchy);
    push(kPageX, ui::NavX);
  }
  if (m.nEvents > 0) push(kPageEvents, ui::NavEvents);
  push(kPageSalmon, ui::NavSalmon);
  push(kPageGear, ui::NavGear);
  push(kPageSettings, ui::NavSettings);
  return n;
}

int defaultPage(const Model& m) {
  return festBattleActive(m) ? kPageFest : kPageRegular;
}

int clampPage(const Model& m, int page) {
  int ids[8];
  int n = fillTabs(m, ids, nullptr, 8);
  for (int i = 0; i < n; ++i)
    if (ids[i] == page) return page;
  return defaultPage(m);
}

int neighborPage(const Model& m, int current, int dir) {
  int ids[8];
  int n = fillTabs(m, ids, nullptr, 8);
  if (n <= 0) return current;
  int idx = 0;
  for (int i = 0; i < n; ++i)
    if (ids[i] == current) {
      idx = i;
      break;
    }
  idx = (idx + dir) % n;
  if (idx < 0) idx += n;
  return ids[idx];
}

static void drawFooter(const Model& m, int pageId) {
  const int y = kH - kFooterH;
  page.fillRect(0, y, kW, kFooterH, C_BLACK);
  int ids[8];
  const char* names[8];
  int n = fillTabs(m, ids, names, 8);
  if (n <= 0) return;
  const int tw = kW / n;
  for (int i = 0; i < n; ++i) {
    int x = i * tw;
    int lw = font24.textWidth(names[i]);
    int tx = x + (tw - lw) / 2;
    int ty = y + 8;  // raised; bar stays at the bottom
    if (ids[i] == pageId) {
      page.fillRect(x + 8, y + 3, tw - 16, 4, C_WHITE);
      font24.draw(&page, tx, ty, names[i], C_WHITE, C_BLACK);
    } else {
      font24.draw(&page, tx, ty, names[i], C_MID, C_BLACK);
    }
  }
}

// --------------------------------------------------------- battle schedules

static int drawSlotList(const ModeSlots* mm, int y, int maxRows, int imgH) {
  if (!mm) return y;
  int cardH = slotCardH(imgH);
  int shown = 0;
  auto row = [&](const Slot& s, bool nowRow) {
    if (shown >= maxRows || y + cardH > kH - kFooterH) return;
    char range[32];
    fmtRange(s.st, s.et, range, sizeof(range));
    uint8_t bg = nowRow ? C_LIGHT : C_WHITE;
    if (nowRow) page.fillRoundRect(12, y, kW - 24, cardH - 4, 4, C_LIGHT);
    const int inset = nowRow ? 16 : 0;
    const int left = 12 + inset;
    const int right = kW - 12 - inset;
    int mw = kMapW;
    if (2 * mw + kMapGap > right - left) mw = (right - left - kMapGap) / 2;
    int x1 = left;
    int x2 = left + mw + kMapGap;
    int tx = nowRow ? x1 : 24;
    int rx = nowRow ? x2 : 260;
    font24.drawEllipsis(&page, tx, y + 8, range, C_BLACK, bg, nowRow ? mw : 220);
    font24.drawEllipsis(&page, rx, y + 8, s.rn, C_DARK, bg, nowRow ? mw : 250);
    int iy = y + 8 + kInk24 + 10;
    drawImgOrBox(x1, iy, mw, imgH, s.si1);
    drawImgOrBox(x2, iy, mw, imgH, s.si2);
    int ny = iy + imgH + 8;
    font24.drawEllipsis(&page, x1, ny, s.s1, C_DARK, bg, mw);
    font24.drawEllipsis(&page, x2, ny, s.s2, C_GRAY, bg, mw);
    y += cardH;
    ++shown;
  };
  uint32_t now = nowEpoch();
  const Slot* list[13];
  int n = 0;
  auto add = [&](const Slot& s) {
    if (!s.st || s.et <= now) return;
    for (int i = 0; i < n; ++i)
      if (list[i]->st == s.st) return;
    if (n < 13) list[n++] = &s;
  };
  if (mm->hasA) add(mm->a);
  for (int i = 0; i < mm->nu; ++i) add(mm->u[i]);
  for (int i = 0; i < n && shown < maxRows; ++i)
    row(*list[i], list[i]->st <= now && now < list[i]->et);
  if (shown == 0) {
    font24.draw(&page, 24, y, ui::None, C_MID, C_WHITE);
    y += kInk24 + 8;
  }
  return y;
}

static void drawBattlePage(const Model& m, const char* title, const char* label,
                           int pageId) {
  drawPageTitle(title);
  const ModeSlots* mm = m.findMode(label);
  int y = kContentY;
  int imgH = kMapHNative;
  int n = fitSlots(y, imgH, 0);
  if (n < 2) {
    imgH = 96;
    n = fitSlots(y, imgH, 0);
  }
  if (!mm || (!mm->hasA && mm->nu == 0)) {
    font24.draw(&page, 24, y, ui::None, C_MID, C_WHITE);
  } else {
    drawSlotList(mm, y, n, imgH);
  }
  drawFooter(m, pageId);
}

static void pickSplitGeom(int listY, int* imgH, int* nPer) {
  int img = kMapHNative;
  int n = fitSlots(listY, img, kSecH) / 2;
  if (n < 2) {
    int avail = kH - kFooterH - listY - kSecH;
    int h = avail / 4;
    img = h - (8 + kInk24 + 10 + 8 + kInk24 + 10);
    if (img < 72) img = 72;
    if (img > kMapHNative) img = kMapHNative;
    n = 2;
    if (fitSlots(listY, img, kSecH) / 2 < 2) n = 1;
  }
  *imgH = img;
  *nPer = n < 1 ? 1 : n;
}

static void drawAnarchyPage(const Model& m) {
  drawPageTitle(ui::AnarchyTitle);
  int y = kContentY;
  font24.draw(&page, 24, y, ui::ModeSeries, C_GRAY, C_WHITE);
  int listY = y + kSecH;
  int imgH, nPer;
  pickSplitGeom(listY, &imgH, &nPer);
  y = drawSlotList(m.findMode(ui::ModeSeries), listY, nPer, imgH);
  font24.draw(&page, 24, y, ui::ModeOpen, C_GRAY, C_WHITE);
  drawSlotList(m.findMode(ui::ModeOpen), y + kSecH, nPer, imgH);
  drawFooter(m, kPageAnarchy);
}

static void drawFestPage(const Model& m) {
  drawPageTitle(ui::FestTitle);
  int y = kContentY;
  if (m.fest.present) {
    font24.drawEllipsis(&page, 24, y, m.fest.title, C_DARK, C_WHITE, kW - 48);
    y += kInk24 + 8;
    y = drawFestTeams(y, m.fest.teams, m.fest.nTeams) + 10;
  }
  font24.draw(&page, 24, y, ui::ModeFestOpen, C_GRAY, C_WHITE);
  int listY = y + kSecH;
  int imgH, nPer;
  pickSplitGeom(listY, &imgH, &nPer);
  y = drawSlotList(m.findMode(ui::ModeFestOpen), listY, nPer, imgH);
  font24.draw(&page, 24, y, ui::ModeFestPro, C_GRAY, C_WHITE);
  drawSlotList(m.findMode(ui::ModeFestPro), y + kSecH, nPer, imgH);
  drawFooter(m, kPageFest);
}

static void drawGearPage(const Model& m) {
  drawPageTitle(ui::GearTitle);
  int y = kContentY;
  if (m.hasMonthly) {
    char line[96];
    snprintf(line, sizeof(line), "%s: %s", ui::MonthlyGear, m.monthly);
    page.fillRoundRect(12, y, kW - 24, kInk24 + 16, 4, C_LIGHT);
    font24.drawEllipsis(&page, 24, y + 8, line, C_BLACK, C_LIGHT, kW - 60);
    y += kInk24 + 24;
  }
  for (int i = 0; i < m.nGear; ++i) {
    const GearItem& g = m.gear[i];
    char ends[24];
    fmtHM(g.et, ends, sizeof(ends));
    char right[96];
    snprintf(right, sizeof(right), "%dG · %s 下架", g.p, ends);
    int rw = font24.textWidth(right);
    drawImgOrBox(16, y, 72, 72, g.img);
    font24.drawEllipsis(&page, 98, y + 8, g.n, C_BLACK, C_WHITE, kW - 140 - rw);
    font24.draw(&page, kW - 24 - rw, y + 8, right, C_MID, C_WHITE);
    font24.drawEllipsis(&page, 98, y + 8 + kLine24, g.pn, C_GRAY, C_WHITE, 400);
    page.drawFastHLine(12, y + 8 + kLine24 + kInk24 + 8, kW - 24, C_LIGHT);
    y += 8 + kLine24 + kInk24 + 16;
  }
  if (m.nGear == 0) drawText(24, y, ui::None, C_MID, C_WHITE);
  drawFooter(m, kPageGear);
}

static void drawAboutPage(const Model& m) {
  drawPageTitle(ui::AboutTitle);
  // FW_VERSION is x.y.z Latin/digits — always present in the URL glyphs.
  // Sits in the title row so it can't collide with the body text below.
  const char* ver = "v" FW_VERSION;
  font24.draw(&page, kW - 24 - font24.textWidth(ver), kTitleY, ver, C_GRAY, C_WHITE);
  font24.draw(&page, 24, kContentY, ui::AppTitle, C_BLACK, C_WHITE);

  static const char* lines[] = {
      "把 splatoon3.ink 的对战、鲑鱼跑、活动与祭典",
      "日程做到 M5Paper 墨水屏上，方便随手查看。",
      "",
      "数据来自 splatoon3.ink，",
      "经 api.splatoon.icu 精简后下发。",
  };
  int y = kContentY + kInk24 + 16;
  for (const char* ln : lines) {
    if (ln[0]) font24.draw(&page, 24, y, ln, C_DARK, C_WHITE);
    y += kLine24;
  }

  y += 16;
  font24.draw(&page, 24, y, ui::AboutGithub, C_GRAY, C_WHITE);
  y += kLine24;
  font24.draw(&page, 24, y, ui::GithubUrl, C_BLACK, C_WHITE);
  y += kInk24 + 16;
  constexpr int qrW = 280;
  int qrX = (kW - qrW) / 2;
  page.qrcode(ui::GithubUrlHttps, qrX, y, qrW, 1, true);
  y += qrW + 20;
  font24.draw(&page, 24, y, ui::TapBack, C_MID, C_WHITE);
  drawFooter(m, kPageSettings);
}

static void drawSettingsPage(const Model& m, const AppStatus& st) {
  if (st.about) {
    drawAboutPage(m);
    return;
  }
  drawPageTitle(ui::SettingsTitle);

  page.fillRoundRect(16, kContentY, kW - 32, 100, 8, C_LIGHT);
  font24.draw(&page, 36, kContentY + 16, ui::WifiSetup, C_BLACK, C_LIGHT);
  char sub[64];
  if (st.wifiSsid[0]) {
    snprintf(sub, sizeof(sub), "%s  %s", ui::CurrentNetwork, st.wifiSsid);
  } else {
    snprintf(sub, sizeof(sub), "%s", ui::NoWifi);
  }
  font24.drawEllipsis(&page, 36, kContentY + 16 + kInk24 + 10, sub, C_DARK, C_LIGHT,
                      kW - 80);

  int y2 = kContentY + 100 + 16;
  page.drawRoundRect(16, y2, kW - 32, 100, 8, C_MID);
  font24.draw(&page, 36, y2 + 16, ui::AboutTitle, C_BLACK, C_WHITE);
  font24.draw(&page, 36, y2 + 16 + kInk24 + 10, ui::AboutHint, C_GRAY, C_WHITE);

  int y3 = y2 + 100 + 16;
  page.fillRoundRect(16, y3, kW - 32, 100, 8, C_LIGHT);
  font24.draw(&page, 36, y3 + 16, ui::RefreshTitle, C_BLACK, C_LIGHT);
  font24.draw(&page, 36, y3 + 16 + kInk24 + 10, ui::RefreshHint, C_DARK, C_LIGHT);

  drawFooter(m, kPageSettings);
}

// ------------------------------------------------------------- page 3 coop --

static int drawShiftRow(int y, const Shift& s) {
  char range[48], line[160];
  fmtRange(s.st, s.et, range, sizeof(range));
  snprintf(line, sizeof(line), "%s%s", s.big ? "▲大型跑 " : "", s.stage);
  int y1 = y + 4;
  font24.drawEllipsis(&page, 24, y1, line, C_BLACK, C_WHITE, 360);
  int rw = font24.textWidth(range);
  font24.draw(&page, kW - 24 - rw, y1, range, C_MID, C_WHITE);
  snprintf(line, sizeof(line), "%s: %s · %s · %s · %s", ui::Weapons,
           weaponShown(s, 0), weaponShown(s, 1), weaponShown(s, 2), weaponShown(s, 3));
  int y2 = y1 + kLine24;
  font24.drawEllipsis(&page, 24, y2, line, C_GRAY, C_WHITE, kW - 60);
  int yLine = y2 + kInk24 + 6;
  page.drawFastHLine(12, yLine, kW - 24, C_LIGHT);
  return yLine + 8;
}

static void drawP3(const Model& m) {
  drawPageTitle(ui::SalmonTitle);

  if (m.nShifts == 0 && m.nEggstra == 0) {
    font24.draw(&page, 24, kContentY, ui::None, C_MID, C_WHITE);
    drawFooter(m, kPageSalmon);
    return;
  }

  int y = kContentY;
  uint32_t now = nowEpoch();
  int i0 = m.liveShiftIndex(now);
  if (i0 >= 0) {
    const Shift& s = m.shifts[i0];
    bool active = s.st <= now && now < s.et;
    char cd[24], tl[64];
    if (active) {
      fmtCountdown(s.et - now, cd, sizeof(cd));
      snprintf(tl, sizeof(tl), "%s%s", ui::Remaining, cd);
    } else {
      fmtCountdown(s.st - now, cd, sizeof(cd));
      snprintf(tl, sizeof(tl), "%s · 开始", cd);
    }
    int tw = font24.textWidth(tl);
    font24.draw(&page, kW - 24 - tw, y + 10, tl, C_DARK, C_WHITE);
    if (s.big) {
      page.fillRoundRect(24, y + 10, 100, kInk24 + 8, 4, C_BLACK);
      font24.draw(&page, 32, y + 14, ui::BigRun, C_WHITE, C_BLACK);
    }
    int iy = y + 10 + kInk24 + 12;
    drawImgOrBox(24, iy, 248, 124, s.si);
    font24.drawEllipsis(&page, 284, iy, s.stage, C_BLACK, C_WHITE, 230);
    if (s.boss[0]) {
      char boss[48];
      snprintf(boss, sizeof(boss), "%s %s", ui::KingSalmonid, s.boss);
      font24.drawEllipsis(&page, 284, iy + kLine24, boss, C_GRAY, C_WHITE, 230);
    }
    int wy = iy + 124 + 12;
    for (int i = 0; i < 4; ++i) {
      int wx = 24 + i * 128;
      drawImgOrBox(wx, wy, 64, 64, s.wi[i]);
      font24.drawEllipsis(&page, wx, wy + 64 + 8, weaponShown(s, i), C_DARK, C_WHITE, 120);
    }
    y += 10 + kInk24 + 12 + 124 + 12 + 64 + 8 + kInk24 + 12;
    page.drawRoundRect(12, kContentY, kW - 24, y - kContentY, 6, C_MID);
  }
  int shown = 0;
  for (int i = (i0 < 0 ? 0 : i0 + 1); i < m.nShifts && shown < 3; ++i) {
    const Shift& s = m.shifts[i];
    if (s.et <= now) continue;
    drawImgOrBox(24, y, 120, 64, s.si);
    char range[48];
    fmtRange(s.st, s.et, range, sizeof(range));
    font24.drawEllipsis(&page, 156, y + 4, s.stage, C_BLACK, C_WHITE, 360);
    font24.draw(&page, 156, y + 4 + kLine24, range, C_MID, C_WHITE);
    y += 4 + kLine24 + kInk24 + 10;
    ++shown;
  }
  if (m.nEggstra > 0) {
    page.fillRoundRect(12, y + 4, kW - 24, kInk24 + 16, 4, C_LIGHT);
    font24.draw(&page, 24, y + 8, ui::Eggstra, C_BLACK, C_LIGHT);
    y += kInk24 + 28;
    for (int i = 0; i < m.nEggstra; ++i) y = drawShiftRow(y, m.eggstra[i]);
  }
  drawFooter(m, kPageSalmon);
}

// ---------------------------------------------------- page 4 events & fest --

static int drawFestTeams(int y, const Team* teams, int n) {
  if (n <= 0) return y;
  if (n > 3) n = 3;
  const int x0 = 12;
  const int colW = (kW - 24) / n;
  const int chip = 24;
  bool anyVr = false;
  for (int i = 0; i < n; ++i)
    if (teams[i].hasVr) anyVr = true;
  for (int i = 0; i < n; ++i) {
    const Team& t = teams[i];
    int x = x0 + i * colW;
    uint8_t lum = (t.r * 30 + t.g * 59 + t.b * 11) / 100;
    uint8_t gray = (uint8_t)(15 - lum * 15 / 255);
    int cy = y + (kInk24 - chip) / 2;
    page.fillRoundRect(x + 4, cy, chip, chip, 4, gray);
    if (t.win) page.drawRoundRect(x + 4, cy, chip, chip, 4, C_BLACK);
    int nx = x + 4 + chip + 6;
    int nw = colW - (chip + 16);
    if (nw < 24) nw = 24;
    font24.drawEllipsis(&page, nx, y, t.n, C_BLACK, C_WHITE, nw);
    if (t.hasVr) {
      char vr[24];
      snprintf(vr, sizeof(vr), "%.1f%%", t.vr / 100.0);
      font24.drawEllipsis(&page, nx, y + kLine24, vr, C_GRAY, C_WHITE, nw);
    }
  }
  return y + kInk24 + (anyVr ? kLine24 : 0) + 8;
}

static void drawP4(const Model& m) {
  char title[48];
  snprintf(title, sizeof(title), "%s / %s", ui::EventsTitle, ui::FestTitle);
  drawPageTitle(title);

  int y = kContentY;
  int eventH = 8 + kInk24 + 10 + kMapHNative + 8 + kInk24 + 10 + kInk24 + 8;
  int room = kH - kFooterH - 200 - y;
  int ne = room / eventH;
  if (ne > m.nEvents) ne = m.nEvents;
  if (ne > 2) ne = 2;
  if (ne < 0) ne = 0;
  if (m.nEvents == 0) {
    drawText(24, y, ui::None, C_MID, C_WHITE);
    y += kInk24 + 10;
  }
  for (int i = 0; i < ne; ++i) {
    const EventItem& e = m.events[i];
    font24.drawEllipsis(&page, 24, y, e.n, C_BLACK, C_WHITE, 360);
    char a[24], b[24], meta[48];
    fmtHM(e.st, a, sizeof(a));
    fmtHM(e.et, b, sizeof(b));
    snprintf(meta, sizeof(meta), "%s~%s", a, b);
    int mw = font24.textWidth(meta);
    font24.draw(&page, kW - 24 - mw, y, meta, C_MID, C_WHITE);
    int iy = y + kInk24 + 10;
    drawImgOrBox(12, iy, kMapW, kMapHNative, e.si1);
    drawImgOrBox(12 + kMapW + kMapGap, iy, kMapW, kMapHNative, e.si2);
    int ny = iy + kMapHNative + 8;
    font24.drawEllipsis(&page, 12, ny, e.s1, C_DARK, C_WHITE, kMapW);
    font24.drawEllipsis(&page, 12 + kMapW + kMapGap, ny, e.s2, C_GRAY, C_WHITE, kMapW);
    int dy = ny + kLine24;
    font24.drawEllipsis(&page, 24, dy, e.d, C_GRAY, C_WHITE, kW - 48);
    y = dy + kInk24 + 12;
  }

  page.drawFastHLine(12, y, kW - 24, C_LIGHT);
  y += 12;
  if (m.fest.present) {
    const FestInfo& f = m.fest;
    font24.drawEllipsis(&page, 24, y, f.title, C_BLACK, C_WHITE, kW - 48);
    y += kLine24;
    char tbuf[96], a[24], b[24];
    fmtHM(f.st, a, sizeof(a));
    fmtHM(f.et, b, sizeof(b));
    const char* phase = !strcmp(f.state, "SECOND_HALF") ? ui::Pro : ui::Open;
    snprintf(tbuf, sizeof(tbuf), "%s · %s ~ %s", phase, a, b);
    font24.draw(&page, 24, y, tbuf, C_GRAY, C_WHITE);
    y += kLine24;
    if (f.mt) {
      char mt[24];
      fmtHM(f.mt, mt, sizeof(mt));
      snprintf(tbuf, sizeof(tbuf), "%s %s", ui::Midterm, mt);
      font24.draw(&page, 24, y, tbuf, C_MID, C_WHITE);
      y += kLine24;
    }
    y = drawFestTeams(y, f.teams, f.nTeams) + 8;
    if (f.nTri > 0) {
      char tri[96];
      snprintf(tri, sizeof(tri), "%s: %s", ui::Tricolor, f.tri[0]);
      font24.drawEllipsis(&page, 24, y, tri, C_GRAY, C_WHITE, kW - 60);
      y += kLine24;
    }
  } else if (m.festNext.present) {
    const FestHist& f = m.festNext;
    drawText(24, y, "下次祭典", C_GRAY, C_WHITE);
    y += kLine24;
    font24.drawEllipsis(&page, 24, y, f.title, C_BLACK, C_WHITE, kW - 48);
    y += kLine24;
    char tbuf[96], a[24], b[24];
    fmtHM(f.st, a, sizeof(a));
    fmtHM(f.et, b, sizeof(b));
    snprintf(tbuf, sizeof(tbuf), "%s ~ %s", a, b);
    font24.draw(&page, 24, y, tbuf, C_MID, C_WHITE);
    y += kLine24;
    y = drawFestTeams(y, f.teams, f.nTeams);
  } else {
    drawText(24, y, "祭典 · 暂无", C_MID, C_WHITE);
  }

  if (m.nFestRecent > 0 && y + 40 + kInk24 + kLine24 <= kH - kFooterH) {
    const FestHist& f = m.festRecent[0];
    y += 12;
    page.drawFastHLine(12, y, kW - 24, C_LIGHT);
    y += 10;
    char tbuf[128];
    snprintf(tbuf, sizeof(tbuf), "%s %s %s", ui::FestTitle, ui::Results, f.title);
    font24.drawEllipsis(&page, 24, y, tbuf, C_DARK, C_WHITE, kW - 60);
    drawFestTeams(y + kLine24, f.teams, f.nTeams);
  }
  drawFooter(m, kPageEvents);
}

// ------------------------------------------------------------------ pages --

void drawPage(const Model& m, const AppStatus& st, bool quality) {
  page.fillSprite(C_WHITE);
  drawHeaderInto(&page, m, st);
  switch (st.page) {
    case kPageRegular:  drawBattlePage(m, ui::ModeRegular, ui::ModeRegular, kPageRegular); break;
    case kPageAnarchy:  drawAnarchyPage(m); break;
    case kPageX:        drawBattlePage(m, ui::ModeX, ui::ModeX, kPageX); break;
    case kPageEvents:   drawP4(m); break;
    case kPageFest:     drawFestPage(m); break;
    case kPageSalmon:   drawP3(m); break;
    case kPageGear:     drawGearPage(m); break;
    case kPageSettings: drawSettingsPage(m, st); break;
    default:            drawBattlePage(m, ui::ModeRegular, ui::ModeRegular, kPageRegular); break;
  }
  pushFull(quality);
}

int footerPageAt(int x, int y, const Model& m) {
  if (y < kH - kFooterH) return -1;
  int ids[8];
  int n = fillTabs(m, ids, nullptr, 8);
  if (n <= 0) return -1;
  int i = x * n / kW;
  if (i < 0) i = 0;
  if (i >= n) i = n - 1;
  return ids[i];
}

int settingsHit(int x, int y, bool about) {
  if (y <= kHeaderH || y >= kH - kFooterH) return 0;
  if (about) return 3;
  (void)x;
  if (y >= kContentY && y < kContentY + 100) return 1;
  int y2 = kContentY + 116;
  if (y >= y2 && y < y2 + 100) return 2;
  int y3 = y2 + 116;
  if (y >= y3 && y < y3 + 100) return 4;
  return 0;
}

bool dumpCanvas() {
  // Snapshot first so later paints can't corrupt the transfer, then stream
  // the packed 4bpp buffer framed with a length + CRC32 (host verifies).
  static uint8_t* snap = nullptr;
  if (!snap) snap = (uint8_t*)ps_malloc(kW * kH / 2);
  const uint8_t* buf = snap ? snap : (const uint8_t*)page.getBuffer();
  if (!buf) return false;
  if (snap) memcpy(snap, page.getBuffer(), kW * kH / 2);
  const size_t len = kW * kH / 2;
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= snap[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ ((-(int32_t)(crc & 1)) & 0xEDB88320u);
  }
  crc = ~crc;

  Serial.printf("#SNAP V2 %d %d 4BPP %u %08X\n", kW, kH, (unsigned)len, crc);
  Serial.flush();
  const uint8_t* p = buf;
  size_t left = len;
  while (left) {
    size_t n = left < 512 ? left : 512;
    Serial.write(p, n);
    p += n;
    left -= n;
  }
  Serial.printf("\n#SNAP END %08X\n", crc);
  return true;
}

}  // namespace render

bool powerDrawSleepHint(M5Canvas& c) {
  c.fillSprite(15);
  constexpr int box = 320;
  int x = (render::kW - box) / 2;
  int y = (render::kH - box) / 2;
  imgDrawContain(&c, x, y, box, box, kBuddyKey);
  return true;
}
