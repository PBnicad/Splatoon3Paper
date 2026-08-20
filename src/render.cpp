#include "render.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <string.h>

#include "config.h"
#include "font.h"
#include "img.h"
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

static void pushFull(bool quality) {
  // Page turns and data redraws use GC16 full-frame refresh to clear ghosts.
  // Minute header ticks keep epd_fastest via refreshHeader().
  M5.Display.setEpdMode(quality ? epd_mode_t::epd_quality : epd_mode_t::epd_fastest);
  page.pushSprite(0, 0);
  if (quality) {
    fastPushes = 0;
  } else if (++fastPushes >= 10) {
    fastPushes = 0;
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    page.pushSprite(0, 0);
  }
}

bool begin() {
  if (M5.Display.width() > M5.Display.height()) {
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  }
  page.setColorDepth(4);
  header.setColorDepth(4);
  bool ok = page.createSprite(kW, kH) != nullptr;
  header.createSprite(kW, kHeaderH);
  return ok;
}

M5Canvas* canvas() { return &page; }

void showStatus(const char* line1, const char* line2) {
  page.fillSprite(C_WHITE);
  font40.draw(&page, 24, 380, line1, C_BLACK, C_WHITE);
  if (line2) font24.draw(&page, 24, 470, line2, C_GRAY, C_WHITE);
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

static int wrapText(const char* s, int maxWidth, char* lines, int maxLines,
                    int lineCap) {
  int n = 0;
  const char* p = s;
  while (*p && n < maxLines) {
    char* out = lines + (size_t)n * lineCap;
    out[0] = 0;
    int w = 0;
    while (*p) {
      const char* glyphStart = p;
      utf8Next(p);
      size_t glen = (size_t)(p - glyphStart);
      char tmp[8];
      if (glen >= sizeof(tmp)) break;
      memcpy(tmp, glyphStart, glen);
      tmp[glen] = 0;
      int gw = font24.textWidth(tmp);
      if (w + gw > maxWidth) break;
      size_t olen = strlen(out);
      if (olen + glen >= (size_t)lineCap) break;
      memcpy(out + olen, glyphStart, glen);
      out[olen + glen] = 0;
      w += gw;
    }
    if (!out[0]) break;
    ++n;
  }
  return n;
}

// ---------------------------------------------------------------- header --

static void drawHeaderInto(M5Canvas* c, const Model& m, const AppStatus& st) {
  (void)m;
  c->fillRect(0, 0, kW, kHeaderH, C_WHITE);
  if (timeValid()) {
    char buf[8];
    fmtClock(nowEpoch(), buf, sizeof(buf));
    font40.draw(c, 16, 4, buf, C_BLACK, C_WHITE);
  }
  int rx = kW - 16;
  drawBattery(c, rx - 29, 18, st.battery);
  rx -= 44;
  drawWifi(c, rx - 16, 18, st.wifiOk, st.offline);
  rx -= 28;
  if (st.offline || st.noWifiConfig) {
    const char* tag = st.noWifiConfig ? ui::NoWifi : ui::Offline;
    int tw = font24.textWidth(tag);
    font24.draw(c, rx - tw, 14, tag, C_BLACK, C_WHITE);
  }
  c->drawFastHLine(0, kHeaderH - 2, kW, C_MID);
}

void refreshHeader(const Model& m, const AppStatus& st) {
  drawHeaderInto(&header, m, st);
  drawHeaderInto(&page, m, st);
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  header.pushSprite(0, 0);
}

// ------------------------------------------------------------- page 1 cards

static void drawImgOrBox(int x, int y, int w, int h, const char* key) {
  page.drawRect(x, y, w, h, C_LIGHT);
  if (!key || !key[0] || !imgDrawFit(&page, x + 1, y + 1, w - 2, h - 2, key)) {
    page.fillRect(x + 1, y + 1, w - 2, h - 2, C_LIGHT);
  }
}

// font24 ink box is ~30px (yoff+h), not the 24px em. Leave that much.

static void drawFestTeams(int y, const Team* teams, int n);

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
    int ty = y + 6;  // raised; bar itself stays at the bottom
    if (ids[i] == pageId) {
      page.fillRect(x + 8, y + 3, tw - 16, 5, C_WHITE);
      font24.draw(&page, tx, ty, names[i], C_WHITE, C_BLACK);
    } else {
      font24.draw(&page, tx, ty, names[i], C_MID, C_BLACK);
    }
  }
}

// --------------------------------------------------------- battle schedules

static int drawSlotList(const ModeSlots* mm, int y, int maxRows) {
  if (!mm) return y;
  int shown = 0;
  auto row = [&](const Slot& s, bool now) {
    if (shown >= maxRows || y + 108 > kH - kFooterH) return;
    char range[32];
    fmtRange(s.st, s.et, range, sizeof(range));
    uint8_t bg = now ? C_LIGHT : C_WHITE;
    if (now) page.fillRoundRect(12, y, kW - 24, 102, 4, C_LIGHT);
    font24.drawEllipsis(&page, 24, y + 4, range, C_BLACK, bg, 200);
    font24.drawEllipsis(&page, 230, y + 4, s.rn, C_DARK, bg, 280);
    drawImgOrBox(24, y + 34, 120, 60, s.si1);
    drawImgOrBox(152, y + 34, 120, 60, s.si2);
    font24.drawEllipsis(&page, 284, y + 40, s.s1, C_DARK, bg, 230);
    font24.drawEllipsis(&page, 284, y + 70, s.s2, C_GRAY, bg, 230);
    y += 106;
    ++shown;
  };
  if (mm->hasA) row(mm->a, true);
  for (int i = 0; i < mm->nu && shown < maxRows; ++i) row(mm->u[i], false);
  if (shown == 0) {
    font24.draw(&page, 24, y, ui::None, C_MID, C_WHITE);
    y += 36;
  }
  return y;
}

static void drawBattlePage(const Model& m, const char* title, const char* label,
                           int pageId) {
  font40.draw(&page, 24, 60, title, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);
  const ModeSlots* mm = m.findMode(label);
  if (!mm || (!mm->hasA && mm->nu == 0)) {
    font40.draw(&page, 24, 140, ui::None, C_MID, C_WHITE);
  } else {
    drawSlotList(mm, 126, 7);
  }
  drawFooter(m, pageId);
}

static void drawAnarchyPage(const Model& m) {
  font40.draw(&page, 24, 60, ui::AnarchyTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);
  int y = 122;
  font24.draw(&page, 24, y, ui::ModeSeries, C_GRAY, C_WHITE);
  y = drawSlotList(m.findMode(ui::ModeSeries), y + 30, 3);
  y += 8;
  font24.draw(&page, 24, y, ui::ModeOpen, C_GRAY, C_WHITE);
  drawSlotList(m.findMode(ui::ModeOpen), y + 30, 3);
  drawFooter(m, kPageAnarchy);
}

static void drawFestPage(const Model& m) {
  font40.draw(&page, 24, 60, ui::FestTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);
  int y = 122;
  if (m.fest.present) {
    font24.drawEllipsis(&page, 24, y, m.fest.title, C_DARK, C_WHITE, kW - 48);
    y += 32;
    drawFestTeams(y, m.fest.teams, m.fest.nTeams);
    y += m.fest.nTeams * 40 + 8;
  }
  font24.draw(&page, 24, y, ui::ModeFestOpen, C_GRAY, C_WHITE);
  y = drawSlotList(m.findMode(ui::ModeFestOpen), y + 30, 2);
  y += 8;
  font24.draw(&page, 24, y, ui::ModeFestPro, C_GRAY, C_WHITE);
  drawSlotList(m.findMode(ui::ModeFestPro), y + 30, 2);
  drawFooter(m, kPageFest);
}

static void drawGearPage(const Model& m) {
  font40.draw(&page, 24, 60, ui::GearTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);
  int y = 126;
  if (m.hasMonthly) {
    char line[96];
    snprintf(line, sizeof(line), "%s: %s", ui::MonthlyGear, m.monthly);
    page.fillRoundRect(12, y, kW - 24, 44, 4, C_LIGHT);
    font24.drawEllipsis(&page, 24, y + 8, line, C_BLACK, C_LIGHT, kW - 60);
    y += 56;
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
    font24.drawEllipsis(&page, 98, y + 42, g.pn, C_GRAY, C_WHITE, 400);
    page.drawFastHLine(12, y + 80, kW - 24, C_LIGHT);
    y += 88;
  }
  if (m.nGear == 0) drawText(24, y, ui::None, C_MID, C_WHITE);
  drawFooter(m, kPageGear);
}

static void drawAboutPage(const Model& m) {
  font40.draw(&page, 24, 60, ui::AboutTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);
  font40.draw(&page, 24, 130, ui::AppTitle, C_BLACK, C_WHITE);

  static const char* lines[] = {
      "把 splatoon3.ink 的对战、鲑鱼跑、活动与祭典",
      "日程做到 M5Paper 墨水屏上，方便随手查看。",
      "",
      "数据来自 splatoon3.ink，",
      "经 api.splatoon.icu 精简后下发。",
  };
  int y = 190;
  for (const char* ln : lines) {
    if (ln[0]) font24.draw(&page, 24, y, ln, C_DARK, C_WHITE);
    y += 32;
  }

  y += 16;
  font24.draw(&page, 24, y, ui::AboutGithub, C_GRAY, C_WHITE);
  y += 36;
  font24.draw(&page, 24, y, ui::GithubUrl, C_BLACK, C_WHITE);
  y += 64;
  font24.draw(&page, 24, y, ui::TapBack, C_MID, C_WHITE);
  drawFooter(m, kPageSettings);
}

static void drawSettingsPage(const Model& m, const AppStatus& st) {
  if (st.about) {
    drawAboutPage(m);
    return;
  }
  font40.draw(&page, 24, 60, ui::SettingsTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);

  page.fillRoundRect(16, 140, kW - 32, 120, 8, C_LIGHT);
  font40.draw(&page, 36, 158, ui::WifiSetup, C_BLACK, C_LIGHT);
  char sub[64];
  if (st.wifiSsid[0]) {
    snprintf(sub, sizeof(sub), "%s  %s", ui::CurrentNetwork, st.wifiSsid);
  } else {
    snprintf(sub, sizeof(sub), "%s", ui::NoWifi);
  }
  font24.drawEllipsis(&page, 36, 214, sub, C_DARK, C_LIGHT, kW - 80);

  page.drawRoundRect(16, 284, kW - 32, 120, 8, C_MID);
  font40.draw(&page, 36, 302, ui::AboutTitle, C_BLACK, C_WHITE);
  font24.draw(&page, 36, 358, ui::AboutHint, C_GRAY, C_WHITE);

  drawFooter(m, kPageSettings);
}

// ------------------------------------------------------------- page 3 coop --

static void drawShiftRow(int y, const Shift& s) {
  char range[48], line[160];
  fmtRange(s.st, s.et, range, sizeof(range));
  snprintf(line, sizeof(line), "%s%s", s.big ? "▲大型跑 " : "", s.stage);
  font24.drawEllipsis(&page, 24, y + 2, line, C_BLACK, C_WHITE, 360);
  int rw = font24.textWidth(range);
  font24.draw(&page, kW - 24 - rw, y + 2, range, C_MID, C_WHITE);
  snprintf(line, sizeof(line), "%s: %s · %s · %s · %s", ui::Weapons,
           weaponShown(s, 0), weaponShown(s, 1), weaponShown(s, 2), weaponShown(s, 3));
  font24.drawEllipsis(&page, 24, y + 28, line, C_GRAY, C_WHITE, kW - 60);
  page.drawFastHLine(12, y + 56, kW - 24, C_LIGHT);
}

static void drawP3(const Model& m) {
  font40.draw(&page, 24, 60, ui::SalmonTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);

  if (m.nShifts == 0 && m.nEggstra == 0) {
    font40.draw(&page, 24, 200, ui::None, C_MID, C_WHITE);
    drawFooter(m, kPageSalmon);
    return;
  }

  int y = 126;
  if (m.nShifts > 0) {
    const Shift& s = m.shifts[0];
    bool active = s.st <= nowEpoch() && nowEpoch() < s.et;
    page.drawRoundRect(12, y, kW - 24, 280, 6, C_MID);
    char cd[24], tl[64];
    if (active) {
      fmtCountdown(s.et - nowEpoch(), cd, sizeof(cd));
      snprintf(tl, sizeof(tl), "%s%s", ui::Remaining, cd);
    } else {
      fmtCountdown(s.st - nowEpoch(), cd, sizeof(cd));
      snprintf(tl, sizeof(tl), "%s · 开始", cd);
    }
    int tw = font24.textWidth(tl);
    font24.draw(&page, kW - 24 - tw, y + 10, tl, C_DARK, C_WHITE);
    if (s.big) {
      page.fillRoundRect(24, y + 10, 100, 30, 4, C_BLACK);
      font24.draw(&page, 32, y + 14, ui::BigRun, C_WHITE, C_BLACK);
    }
    drawImgOrBox(24, y + 46, 248, 124, s.si);
    font40.drawEllipsis(&page, 284, y + 50, s.stage, C_BLACK, C_WHITE, 230);
    if (s.boss[0]) {
      char boss[48];
      snprintf(boss, sizeof(boss), "%s %s", ui::KingSalmonid, s.boss);
      font24.drawEllipsis(&page, 284, y + 96, boss, C_GRAY, C_WHITE, 230);
    }
    for (int i = 0; i < 4; ++i) {
      int wx = 24 + i * 128;
      drawImgOrBox(wx, y + 180, 64, 64, s.wi[i]);
      font24.drawEllipsis(&page, wx + 68, y + 196, weaponShown(s, i), C_DARK, C_WHITE, 56);
    }
    y += 292;
  }
  for (int i = 1; i < m.nShifts && i <= 3; ++i, y += 76) {
    const Shift& s = m.shifts[i];
    drawImgOrBox(24, y, 120, 60, s.si);
    char range[48];
    fmtRange(s.st, s.et, range, sizeof(range));
    font24.drawEllipsis(&page, 156, y + 4, s.stage, C_BLACK, C_WHITE, 360);
    font24.draw(&page, 156, y + 38, range, C_MID, C_WHITE);
  }
  if (m.nEggstra > 0) {
    page.fillRoundRect(12, y + 4, kW - 24, 36, 4, C_LIGHT);
    font24.draw(&page, 24, y + 10, ui::Eggstra, C_BLACK, C_LIGHT);
    y += 48;
    for (int i = 0; i < m.nEggstra; ++i, y += 62) drawShiftRow(y, m.eggstra[i]);
  }
  drawFooter(m, kPageSalmon);
}

// ---------------------------------------------------- page 4 events & fest --

static int drawEventCard(int y, const EventItem& e) {
  bool active = e.st <= nowEpoch() && nowEpoch() < e.et;
  char meta[96], a[24], b[24];
  fmtHM(e.st, a, sizeof(a));
  fmtHM(e.et, b, sizeof(b));
  snprintf(meta, sizeof(meta), "%s%s ~ %s", active ? ui::NowOpen : "", a, b);

  page.drawRoundRect(12, y, kW - 24, 8, 4, C_MID);
  font40.drawEllipsis(&page, 24, y + 18, e.n, C_BLACK, C_WHITE, kW - 250);
  int mw = font24.textWidth(meta);
  font24.draw(&page, kW - 24 - mw, y + 26, meta, active ? C_BLACK : C_MID, C_WHITE);

  char line[128];
  snprintf(line, sizeof(line), "%s · %s · %s", e.rn, e.s1, e.s2);
  font24.drawEllipsis(&page, 24, y + 68, line, C_DARK, C_WHITE, kW - 60);

  char lines[3][80];
  int nl = wrapText(e.d, kW - 60, lines[0], 2, sizeof(lines[0]));
  for (int i = 0; i < nl; ++i) font24.draw(&page, 24, y + 100 + i * 30, lines[i], C_GRAY, C_WHITE);
  y += 100 + nl * 30;

  nl = wrapText(e.r, kW - 60, lines[0], 3, sizeof(lines[0]));
  for (int i = 0; i < nl; ++i) font24.draw(&page, 24, y + i * 28, lines[i], C_MID, C_WHITE);
  y += nl * 28;

  int np = e.np > 6 ? 6 : e.np;
  for (int i = 0; i < np; ++i) {
    char range[32];
    fmtRange(e.p[i].st, e.p[i].et, range, sizeof(range));
    bool now = e.p[i].st <= nowEpoch() && nowEpoch() < e.p[i].et;
    font24.draw(&page, 40, y + i * 26, range, now ? C_BLACK : C_MID, C_WHITE);
  }
  return y + np * 26 + 14;
}

static void drawFestTeams(int y, const Team* teams, int n) {
  for (int i = 0; i < n; ++i) {
    const Team& t = teams[i];
    uint8_t lum = (t.r * 30 + t.g * 59 + t.b * 11) / 100;
    uint8_t gray = (uint8_t)(15 - lum * 15 / 255);  // dark color → dark gray
    page.fillRoundRect(24, y + i * 40, 40, 28, 4, gray);
    font24.drawEllipsis(&page, 80, y + i * 40 + 2, t.n, C_BLACK, C_WHITE, 200);
    if (t.win) font24.draw(&page, 300, y + i * 40 + 2, ui::Won, C_BLACK, C_WHITE);
    if (t.hasVr) {
      char vr[32];
      snprintf(vr, sizeof(vr), "%s %.1f%%", ui::Votes, t.vr / 100.0);
      font24.draw(&page, kW - 24 - font24.textWidth(vr), y + i * 40 + 2, vr, C_GRAY, C_WHITE);
    }
  }
}

static void drawP4(const Model& m) {
  font40.draw(&page, 24, 60, ui::EventsTitle, C_BLACK, C_WHITE);
  font40.draw(&page, 24 + font40.textWidth(ui::EventsTitle) + 24, 60, "/", C_MID, C_WHITE);
  font40.draw(&page, 24 + font40.textWidth(ui::EventsTitle) + 72, 60, ui::FestTitle,
              C_BLACK, C_WHITE);
  page.drawFastHLine(0, 112, kW, C_MID);

  int y = 126;
  int ne = m.nEvents > 2 ? 2 : m.nEvents;
  if (ne == 0) {
    drawText(24, y, ui::None, C_MID, C_WHITE);
    y += 40;
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
    drawImgOrBox(24, y + 34, 248, 80, e.si1);
    drawImgOrBox(280, y + 34, 248, 80, e.si2);
    font24.drawEllipsis(&page, 24, y + 118, e.d, C_GRAY, C_WHITE, kW - 48);
    y += 154;
  }

  page.drawFastHLine(12, y, kW - 24, C_LIGHT);
  y += 12;
  if (m.fest.present) {
    const FestInfo& f = m.fest;
    font40.drawEllipsis(&page, 24, y, f.title, C_BLACK, C_WHITE, kW - 200);
    char tbuf[96], a[24], b[24];
    fmtHM(f.st, a, sizeof(a));
    fmtHM(f.et, b, sizeof(b));
    const char* phase = !strcmp(f.state, "SECOND_HALF") ? ui::Pro : ui::Open;
    snprintf(tbuf, sizeof(tbuf), "%s · %s ~ %s", phase, a, b);
    font24.draw(&page, 24, y + 52, tbuf, C_GRAY, C_WHITE);
    if (f.mt) {
      char mt[24];
      fmtHM(f.mt, mt, sizeof(mt));
      snprintf(tbuf, sizeof(tbuf), "%s %s", ui::Midterm, mt);
      font24.draw(&page, 24, y + 84, tbuf, C_MID, C_WHITE);
    }
    drawFestTeams(y + 116, f.teams, f.nTeams);
    if (f.nTri > 0) {
      char tri[96];
      snprintf(tri, sizeof(tri), "%s: %s", ui::Tricolor, f.tri[0]);
      font24.drawEllipsis(&page, 24, y + 236, tri, C_GRAY, C_WHITE, kW - 60);
    }
  } else if (m.festNext.present) {
    const FestHist& f = m.festNext;
    drawText(24, y, "下次祭典", C_GRAY, C_WHITE);
    font40.drawEllipsis(&page, 24, y + 34, f.title, C_BLACK, C_WHITE, kW - 60);
    char tbuf[96], a[24], b[24];
    fmtHM(f.st, a, sizeof(a));
    fmtHM(f.et, b, sizeof(b));
    snprintf(tbuf, sizeof(tbuf), "%s ~ %s", a, b);
    font24.draw(&page, 24, y + 86, tbuf, C_MID, C_WHITE);
    drawFestTeams(y + 122, f.teams, f.nTeams);
  } else {
    drawText(24, y, "祭典 · 暂无", C_MID, C_WHITE);
  }

  if (m.nFestRecent > 0) {
    const FestHist& f = m.festRecent[0];
    int ry = 712;
    page.drawFastHLine(12, ry - 10, kW - 24, C_LIGHT);
    char tbuf[128];
    snprintf(tbuf, sizeof(tbuf), "%s %s %s", ui::FestTitle, ui::Results, f.title);
    font24.drawEllipsis(&page, 24, ry + 2, tbuf, C_DARK, C_WHITE, kW - 60);
    drawFestTeams(ry + 34, f.teams, f.nTeams);
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
  if (y >= 140 && y < 260) return 1;
  if (y >= 284 && y < 404) return 2;
  return 0;
}

bool dumpCanvas() {
  const uint8_t* buf = (const uint8_t*)page.getBuffer();
  if (!buf) return false;
  // 4bpp packed (high nibble first), row-major → 8-bit gray PGM P5
  Serial.print("P5\n540 960\n255\n");
  uint8_t out[540];
  for (int y = 0; y < 960; ++y) {
    const uint8_t* row = buf + (y * 540 * 4) / 8;
    for (int x = 0; x < 540; ++x) {
      uint8_t b = row[x >> 1];
      out[x] = (((x & 1) ? (b & 0x0F) : (b >> 4)) << 4) | 0x0F;
    }
    Serial.write(out, sizeof(out));
  }
  return true;
}

}  // namespace render

bool powerDrawSleepHint(M5Canvas& c) {
  constexpr int bw = 168, bh = 168;
  int x = (render::kW - bw) / 2;
  int y = (render::kH - bh) / 2 - 50;
  imgDrawFit(&c, x, y, bw, bh, kBuddyKey);
  if (font24.valid()) {
    const char* msg = ui::SleepHint;
    int w = font24.textWidth(msg);
    font24.draw(&c, (render::kW - w) / 2, y + bh + 24, msg, 15, 0);
  }
  return true;
}
