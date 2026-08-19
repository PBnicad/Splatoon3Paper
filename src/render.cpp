#include "render.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <string.h>

#include "config.h"
#include "font.h"
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
  c->fillSprite(C_WHITE);
  if (st.offline || st.noWifiConfig) c->fillRect(0, 0, kW, 6, C_BLACK);
  font24.draw(c, 16, 14, ui::AppTitle, C_GRAY, C_WHITE);

  if (timeValid()) {
    char buf[8];
    fmtClock(nowEpoch(), buf, sizeof(buf));
    int w = font96.textWidth(buf);
    font96.draw(c, kW - 16 - w, 6, buf, C_BLACK, C_WHITE);
  }

  char left[96];
  if (st.noWifiConfig) {
    snprintf(left, sizeof(left), "%s", ui::NoWifiLine2);
  } else {
    char tbuf[8] = "--:--";
    if (st.lastFetchOk) fmtClock(st.lastFetchOk, tbuf, sizeof(tbuf));
    snprintf(left, sizeof(left), "%s %s%s", ui::Updated, tbuf,
             st.offline ? ui::Offline : "");
  }
  font24.draw(c, 16, 78, left, st.offline ? C_BLACK : C_GRAY, C_WHITE);

  if (m.nf > nowEpoch()) {
    char cd[24], right[48];
    fmtCountdown(m.nf - nowEpoch(), cd, sizeof(cd));
    snprintf(right, sizeof(right), "换挡 %s", cd);
    font24.draw(c, kW - 258, 44, right, C_DARK, C_WHITE);
  }
  drawWifi(c, kW - 92, 78, st.wifiOk, st.offline);
  drawBattery(c, kW - 62, 76, st.battery);

  c->drawFastHLine(0, kHeaderH - 2, kW, C_MID);
}

void refreshHeader(const Model& m, const AppStatus& st) {
  drawHeaderInto(&header, m, st);
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  header.pushSprite(0, 0);
}

// ------------------------------------------------------------- page 1 cards

static void drawModeCard(int y, const char* label, const Slot& s, bool has) {
  constexpr int kCardH = 136;
  page.fillRoundRect(12, y, kW - 24, kCardH, 6, C_WHITE);
  page.drawRoundRect(12, y, kW - 24, kCardH, 6, C_LIGHT);
  page.drawFastVLine(16, y + 10, kCardH - 20, C_MID);

  font24.draw(&page, 26, y + 10, label, C_GRAY, C_WHITE);
  if (!has) {
    font40.draw(&page, 26, y + 44, ui::None, C_MID, C_WHITE);
    return;
  }
  char range[32];
  fmtRange(s.st, s.et, range, sizeof(range));
  int rw = font24.textWidth(range);
  font24.draw(&page, kW - 24 - rw, y + 10, range, C_MID, C_WHITE);

  font40.drawEllipsis(&page, 26, y + 38, s.rn, C_BLACK, C_WHITE, kW - 60);

  page.fillCircle(20, y + 102, 3, C_MID);
  font24.drawEllipsis(&page, 30, y + 90, s.s1, C_DARK, C_WHITE, kW - 70);
  page.fillCircle(20, y + 122, 3, C_MID);
  font24.drawEllipsis(&page, 30, y + 110, s.s2, C_DARK, C_WHITE, kW - 70);
}

static void drawBanner(const Model& m) {
  int y = 694;
  char text[128];
  if (m.fest.present) {
    snprintf(text, sizeof(text), "%s · %s", ui::FestTitle, m.fest.title);
  } else if (m.nEvents > 0) {
    char buf[24];
    fmtHM(m.events[0].st, buf, sizeof(buf));
    snprintf(text, sizeof(text), "%s · %s · %s%s", ui::EventsTitle,
             m.events[0].n, buf, ui::Within);
  } else {
    return;
  }
  page.fillRoundRect(12, y, kW - 24, 50, 6, C_BLACK);
  font24.drawEllipsis(&page, 24, y + 13, text, C_WHITE, C_BLACK, kW - 60);
}

static const char* weaponShown(const Shift& s, int i) {
  if (s.mys && strcmp(s.w[i], "随机") == 0)
    return s.gmys ? ui::GrizzcoRandom : ui::RandomWeapon;
  return s.w[i];
}

static void drawSalmonSummary(const Model& m) {
  int y = 752;
  page.fillRoundRect(12, y, kW - 24, 162, 6, C_WHITE);
  page.drawRoundRect(12, y, kW - 24, 162, 6, C_LIGHT);
  font24.draw(&page, 26, y + 10, ui::SalmonTitle, C_GRAY, C_WHITE);

  if (m.nShifts == 0) {
    font40.draw(&page, 26, y + 44, ui::None, C_MID, C_WHITE);
    return;
  }
  const Shift& s = m.shifts[0];
  bool active = s.st <= nowEpoch() && nowEpoch() < s.et;
  if (s.big) {
    page.fillRoundRect(110, y + 6, 96, 30, 4, C_BLACK);
    font24.draw(&page, 118, y + 11, ui::BigRun, C_WHITE, C_BLACK);
  }
  char timeBuf[48];
  char cd[24];
  if (active) {
    fmtCountdown(s.et - nowEpoch(), cd, sizeof(cd));
    snprintf(timeBuf, sizeof(timeBuf), "%s%s", ui::Remaining, cd);
  } else {
    fmtCountdown(s.st - nowEpoch(), cd, sizeof(cd));
    snprintf(timeBuf, sizeof(timeBuf), "%s%s", ui::Within, cd);
  }
  int tw = font24.textWidth(timeBuf);
  font24.draw(&page, kW - 24 - tw, y + 10, timeBuf, C_MID, C_WHITE);

  font40.drawEllipsis(&page, 26, y + 42, s.stage, C_BLACK, C_WHITE, 300);
  if (s.boss[0]) {
    char boss[48];
    snprintf(boss, sizeof(boss), "%s %s", ui::KingSalmonid, s.boss);
    font24.draw(&page, kW - 24 - font24.textWidth(boss), y + 52, boss, C_GRAY, C_WHITE);
  }

  char wline[160];
  int off = snprintf(wline, sizeof(wline), "%s: ", ui::Weapons);
  for (int i = 0; i < 4; ++i) {
    if (!s.w[i][0]) break;
    int add = snprintf(nullptr, 0, "%s%s", i ? " · " : "", weaponShown(s, i));
    if (off + add >= (int)sizeof(wline) - 1) break;
    off += snprintf(wline + off, sizeof(wline) - off, "%s%s", i ? " · " : "",
                    weaponShown(s, i));
  }
  font24.drawEllipsis(&page, 26, y + 96, wline, C_DARK, C_WHITE, kW - 60);

  if (m.nShifts > 1) {
    char next[96], buf[24];
    fmtHM(m.shifts[1].st, buf, sizeof(buf));
    snprintf(next, sizeof(next), "%s %s %s", ui::Next, buf, m.shifts[1].stage);
    font24.drawEllipsis(&page, 26, y + 128, next, C_GRAY, C_WHITE, kW - 60);
  }
}

static void drawFooter(int pageIdx) {
  int y = kH - 40;
  page.drawFastHLine(0, y - 8, kW, C_LIGHT);
  static const char* names[kPageCount] = {"总览", "时段", "鲑鱼跑", "活动祭典", "商店"};
  for (int i = 0; i < kPageCount; ++i) {
    int cx = 24 + i * 20;
    if (i == pageIdx) page.fillCircle(cx, y + 14, 6, C_BLACK);
    else page.drawCircle(cx, y + 14, 5, C_MID);
  }
  font24.draw(&page, 160, y, names[pageIdx], C_GRAY, C_WHITE);
  font24.draw(&page, kW - 24 - font24.textWidth(ui::Attribution), y,
              ui::Attribution, C_LIGHT, C_WHITE);
}

static void drawP1(const Model& m, const AppStatus& st) {
  (void)st;
  const ModeSlots* cards[4] = {nullptr, nullptr, nullptr, nullptr};
  int nc = 0;
  if (m.fest.present) {
    // site parity: during a fest, fest boxes replace regular/anarchy/X
    for (int i = 0; i < m.nModes && nc < 4; ++i)
      if (!strcmp(m.modes[i].label, ui::ModeFestOpen) ||
          !strcmp(m.modes[i].label, ui::ModeFestPro))
        cards[nc++] = &m.modes[i];
    for (int i = 0; i < m.nModes && nc < 4; ++i)
      if (strcmp(m.modes[i].label, ui::ModeFestOpen) &&
          strcmp(m.modes[i].label, ui::ModeFestPro) &&
          strcmp(m.modes[i].label, ui::ModeRegular))
        cards[nc++] = &m.modes[i];
  } else {
    for (int i = 0; i < m.nModes && nc < 4; ++i) cards[nc++] = &m.modes[i];
  }
  int y = 120;
  for (int i = 0; i < nc; ++i, y += 144)
    drawModeCard(y, cards[i]->label, cards[i]->a, cards[i]->hasA);
  drawBanner(m);
  drawSalmonSummary(m);
}

// ------------------------------------------------------------- page 2 list --

static void drawP2(const Model& m, const AppStatus& st) {
  int ntabs = m.nModes;
  if (ntabs == 0) {
    font40.draw(&page, 24, 130, ui::None, C_MID, C_WHITE);
    return;
  }
  int tab = st.p2Tab < ntabs ? st.p2Tab : 0;
  int tw = kW / ntabs;
  for (int i = 0; i < ntabs; ++i) {
    int x = i * tw;
    if (i == tab) page.fillRect(x, 118, tw - 2, 52, C_BLACK);
    char label[24];
    utf8Truncate(label, sizeof(label), m.modes[i].label, 5);
    font24.drawEllipsis(&page, x + 12, 132, label,
                        i == tab ? C_WHITE : C_DARK,
                        i == tab ? C_BLACK : C_WHITE, tw - 24);
  }
  page.drawFastHLine(0, 172, kW, C_MID);

  const ModeSlots& mm = m.modes[tab];
  drawText(24, 184, "接下来 24 小时", C_GRAY, C_WHITE);

  int y = 222;
  int shown = 0;
  if (mm.hasA) {
    page.fillRoundRect(12, y, kW - 24, 56, 4, C_LIGHT);
    char range[32], line[128];
    fmtRange(mm.a.st, mm.a.et, range, sizeof(range));
    snprintf(line, sizeof(line), "%s  %s", range, mm.a.rn);
    font24.drawEllipsis(&page, 24, y + 2, line, C_BLACK, C_LIGHT, kW - 60);
    snprintf(line, sizeof(line), "%s · %s", mm.a.s1, mm.a.s2);
    font24.drawEllipsis(&page, 24, y + 28, line, C_DARK, C_LIGHT, kW - 60);
    y += 62;
    ++shown;
  }
  for (int i = 0; i < mm.nu && shown < 12; ++i, ++shown) {
    const Slot& s = mm.u[i];
    char range[32], line[128];
    fmtRange(s.st, s.et, range, sizeof(range));
    snprintf(line, sizeof(line), "%s  %s", range, s.rn);
    font24.drawEllipsis(&page, 24, y + 2, line, C_DARK, C_WHITE, kW - 60);
    char stages[96];
    snprintf(stages, sizeof(stages), "%s · %s", s.s1, s.s2);
    font24.drawEllipsis(&page, 24, y + 28, stages, C_GRAY, C_WHITE, kW - 60);
    page.drawFastHLine(12, y + 56, kW - 24, C_LIGHT);
    y += 60;
  }
  drawFooter(1);
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
  font40.draw(&page, 24, 122, ui::SalmonTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 174, kW, C_MID);

  if (m.nShifts == 0 && m.nEggstra == 0) {
    font40.draw(&page, 24, 200, ui::None, C_MID, C_WHITE);
    drawFooter(2);
    return;
  }

  int y = 190;
  if (m.nShifts > 0) {
    const Shift& s = m.shifts[0];
    bool active = s.st <= nowEpoch() && nowEpoch() < s.et;
    page.drawRoundRect(12, y, kW - 24, 240, 6, C_MID);
    if (s.big) {
      page.fillRoundRect(24, y + 12, 100, 34, 4, C_BLACK);
      font24.draw(&page, 32, y + 17, ui::BigRun, C_WHITE, C_BLACK);
    }
    char cd[24], tl[64];
    if (active) {
      fmtCountdown(s.et - nowEpoch(), cd, sizeof(cd));
      snprintf(tl, sizeof(tl), "%s%s", ui::Remaining, cd);
    } else {
      fmtCountdown(s.st - nowEpoch(), cd, sizeof(cd));
      snprintf(tl, sizeof(tl), "%s · 开始", cd);
    }
    int tw = font24.textWidth(tl);
    font24.draw(&page, kW - 24 - tw, y + 12, tl, C_DARK, C_WHITE);

    font40.drawEllipsis(&page, 24, y + 54, s.stage, C_BLACK, C_WHITE, kW - 260);
    if (s.boss[0]) {
      char boss[48];
      snprintf(boss, sizeof(boss), "%s %s", ui::KingSalmonid, s.boss);
      font24.draw(&page, kW - 24 - font24.textWidth(boss), y + 64, boss, C_GRAY, C_WHITE);
    }
    for (int i = 0; i < 4; ++i) {
      if (!s.w[i][0]) break;
      int wx = 28 + (i % 2) * 254;
      int wy = y + 118 + (i / 2) * 44;
      page.fillCircle(wx - 10, wy + 12, 3, C_MID);
      font24.drawEllipsis(&page, wx, wy, weaponShown(s, i), C_DARK, C_WHITE, 236);
    }
    y += 252;
  }
  for (int i = 1; i < m.nShifts && i <= 4; ++i, y += 62) drawShiftRow(y, m.shifts[i]);
  if (m.nEggstra > 0) {
    page.fillRoundRect(12, y + 4, kW - 24, 36, 4, C_LIGHT);
    font24.draw(&page, 24, y + 10, ui::Eggstra, C_BLACK, C_LIGHT);
    y += 48;
    for (int i = 0; i < m.nEggstra; ++i, y += 62) drawShiftRow(y, m.eggstra[i]);
  }
  drawFooter(2);
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
  font40.draw(&page, 24, 122, ui::EventsTitle, C_BLACK, C_WHITE);
  font40.draw(&page, 24 + font40.textWidth(ui::EventsTitle) + 24, 122, "/", C_MID, C_WHITE);
  font40.draw(&page, 24 + font40.textWidth(ui::EventsTitle) + 72, 122, ui::FestTitle,
              C_BLACK, C_WHITE);
  page.drawFastHLine(0, 174, kW, C_MID);

  int y = 190;
  for (int i = 0; i < m.nEvents && y < 620; ++i) y = drawEventCard(y, m.events[i]);
  if (m.nEvents == 0) {
    drawText(24, y, ui::None, C_MID, C_WHITE);
    y += 40;
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
  drawFooter(3);
}

// ------------------------------------------------------------- page 5 shop --

static void drawP5(const Model& m) {
  font40.draw(&page, 24, 122, ui::GearTitle, C_BLACK, C_WHITE);
  page.drawFastHLine(0, 174, kW, C_MID);

  int y = 190;
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
    font24.drawEllipsis(&page, 24, y + 4, g.n, C_BLACK, C_WHITE, 300);
    char right[96];
    snprintf(right, sizeof(right), "%dG · %s 下架", g.p, ends);
    font24.draw(&page, kW - 24 - font24.textWidth(right), y + 4, right, C_MID, C_WHITE);
    font24.drawEllipsis(&page, 40, y + 32, g.pn, C_GRAY, C_WHITE, 400);
    page.drawFastHLine(12, y + 62, kW - 24, C_LIGHT);
    y += 70;
  }
  if (m.nGear == 0) drawText(24, y, ui::None, C_MID, C_WHITE);
  drawFooter(4);
}

// ------------------------------------------------------------------ pages --

void drawPage(const Model& m, const AppStatus& st, bool quality) {
  page.fillSprite(C_WHITE);
  switch (st.page) {
    case 0: drawP1(m, st); break;
    case 1: drawP2(m, st); break;
    case 2: drawP3(m); break;
    case 3: drawP4(m); break;
    case 4: drawP5(m); break;
    default: break;
  }
  pushFull(quality);
  refreshHeader(m, st);
}

}  // namespace render

bool powerDrawSleepHint(M5Canvas& c) {
  if (!font40.valid()) return false;
  const char* msg = ui::SleepHint;
  int w = font40.textWidth(msg);
  font40.draw(&c, (render::kW - w) / 2, render::kH / 2 - 20, msg, 15, 0);
  return true;
}
