#include "wifiui.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "font.h"
#include "net.h"
#include "render.h"

namespace {

enum { C_BLACK = 0, C_DARK = 3, C_GRAY = 6, C_MID = 9, C_LIGHT = 12, C_WHITE = 15 };
constexpr int kW = 540, kH = 960;
constexpr int kInk24 = 36;
constexpr int kInk40 = 58;
constexpr int kListY = 184;

const char* kRows[4] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
};

struct ScanRow {
  char ssid[33];
  int rssi;
  bool open;
};

ScanRow gNets[16];
int gN = 0;
int gSel = -1;
char gPass[65];
int gPassLen = 0;
bool gShift = false;
enum Ui { WELCOME, LIST, PASS, BAD } gUi = WELCOME;
bool gFirst = false;

void fullPush(M5Canvas& c) {
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  c.pushSprite(0, 0);
}

void drawRssi(M5Canvas& c, int x, int y, int rssi) {
  int bars = rssi >= -55 ? 3 : rssi >= -70 ? 2 : 1;
  for (int i = 0; i < 3; i++) {
    int h = 8 + i * 7;
    int bx = x + i * 9;
    int by = y + 22 - h;
    if (i < bars) c.fillRect(bx, by, 7, h, C_BLACK);
    else c.drawRect(bx, by, 7, h, C_MID);
  }
}

void drawKb(M5Canvas& c, int y0) {
  c.drawFastHLine(0, y0, kW, C_MID);
  for (int r = 0; r < 4; r++) {
    const char* row = kRows[r];
    int n = (int)strlen(row);
    int cw = kW / n;
    int y = y0 + 8 + r * 56;
    for (int i = 0; i < n; i++) {
      char t[2] = {row[i], 0};
      if (gShift && t[0] >= 'a' && t[0] <= 'z') t[0] = (char)(t[0] - 32);
      int x = i * cw;
      c.drawRoundRect(x + 3, y, cw - 6, 48, 4, C_MID);
      font24.draw(&c, x + cw / 2 - 6, y + 9, t, C_BLACK, C_WHITE);
    }
  }
  int y = y0 + 8 + 4 * 56;
  c.fillRoundRect(12, y, 90, 52, 4, C_LIGHT);
  font24.draw(&c, 28, y + 11, "Aa", C_BLACK, C_LIGHT);
  c.fillRoundRect(114, y, 220, 52, 4, C_LIGHT);
  font24.draw(&c, 186, y + 11, "空格", C_BLACK, C_LIGHT);
  c.fillRoundRect(346, y, 80, 52, 4, C_LIGHT);
  font24.draw(&c, 366, y + 11, "删", C_BLACK, C_LIGHT);
  c.fillRoundRect(438, y, 90, 52, 4, C_BLACK);
  font24.draw(&c, 454, y + 11, ui::Connect, C_WHITE, C_BLACK);
}

void drawWelcome(M5Canvas& c) {
  c.fillSprite(C_WHITE);
  font40.draw(&c, 36, 88, ui::WelcomeHi, C_BLACK, C_WHITE);
  font40.draw(&c, 36, 88 + kInk40 + 16, ui::AppTitle, C_BLACK, C_WHITE);
  c.drawFastHLine(36, 88 + 2 * (kInk40 + 16), kW - 72, C_LIGHT);
  font24.draw(&c, 36, 260, ui::WelcomeBody1, C_DARK, C_WHITE);
  font24.draw(&c, 36, 260 + kInk24 + 12, ui::WelcomeBody2, C_DARK, C_WHITE);
  font24.draw(&c, 36, 260 + 2 * (kInk24 + 12), ui::WelcomeBody3, C_DARK, C_WHITE);

  c.fillRoundRect(36, 668, kW - 72, 104, 8, C_BLACK);
  int tw = font40.textWidth(ui::StartWifi);
  font40.draw(&c, (kW - tw) / 2, 668 + (104 - kInk40) / 2, ui::StartWifi, C_WHITE, C_BLACK);

  int lw = font24.textWidth(ui::Later);
  font24.draw(&c, (kW - lw) / 2, 800, ui::Later, C_MID, C_WHITE);
  fullPush(c);
}

void drawScanning(M5Canvas& c) {
  c.fillSprite(C_WHITE);
  font40.draw(&c, 36, 380, ui::Scanning, C_BLACK, C_WHITE);
  fullPush(c);
}

void drawList(M5Canvas& c) {
  c.fillSprite(C_WHITE);
  const char* left = gFirst ? ui::Later : ui::Back;
  font24.draw(&c, 24, 18, left, C_DARK, C_WHITE);
  font24.draw(&c, kW - 24 - font24.textWidth(ui::Refresh), 18, ui::Refresh, C_DARK,
              C_WHITE);
  font40.draw(&c, 24, 64, ui::PickWifi, C_BLACK, C_WHITE);
  font24.draw(&c, 24, 64 + kInk40 + 12, "点选网络，下一步输入密码", C_GRAY, C_WHITE);

  int y = kListY;
  for (int i = 0; i < gN && i < 10; i++) {
    if (i == gSel) c.fillRoundRect(16, y, kW - 32, 64, 6, C_LIGHT);
    else c.drawRoundRect(16, y, kW - 32, 64, 6, C_LIGHT);
    uint8_t bg = (i == gSel) ? C_LIGHT : C_WHITE;
    font24.drawEllipsis(&c, 32, y + 16, gNets[i].ssid, C_BLACK, bg,
                        gNets[i].open ? 300 : 380);
    if (gNets[i].open)
      font24.draw(&c, kW - 160, y + 16, ui::Open, C_GRAY, bg);
    drawRssi(c, kW - 70, y + 20, gNets[i].rssi);
    y += 70;
  }
  if (gN == 0) font24.draw(&c, 32, 200, "未找到网络，点右上角刷新", C_GRAY, C_WHITE);
  fullPush(c);
}

void drawPass(M5Canvas& c) {
  c.fillSprite(C_WHITE);
  font24.draw(&c, 24, 16, ui::Back, C_DARK, C_WHITE);
  font40.drawEllipsis(&c, 24, 56, gSel >= 0 ? gNets[gSel].ssid : "", C_BLACK, C_WHITE,
                      kW - 48);
  int boxY = 56 + kInk40 + 12;
  c.drawRoundRect(16, boxY, kW - 32, 56, 4, C_MID);
  if (gPassLen == 0) font24.draw(&c, 32, boxY + 13, ui::EnterPass, C_MID, C_WHITE);
  else font24.drawEllipsis(&c, 32, boxY + 13, gPass, C_BLACK, C_WHITE, kW - 80);
  drawKb(c, 188);
  fullPush(c);
}

void drawFail(M5Canvas& c) {
  c.fillSprite(C_WHITE);
  font40.draw(&c, 36, 200, ui::ConnectFail, C_BLACK, C_WHITE);
  font24.draw(&c, 36, 200 + kInk40 + 16, ui::CheckPass, C_DARK, C_WHITE);
  if (gSel >= 0)
    font24.drawEllipsis(&c, 36, 200 + kInk40 + 16 + kInk24 + 10, gNets[gSel].ssid, C_GRAY,
                        C_WHITE, kW - 72);

  c.fillRoundRect(36, 500, kW - 72, 80, 8, C_BLACK);
  int tw = font24.textWidth(ui::RetryPass);
  font24.draw(&c, (kW - tw) / 2, 524, ui::RetryPass, C_WHITE, C_BLACK);

  c.drawRoundRect(36, 604, kW - 72, 80, 8, C_MID);
  tw = font24.textWidth(ui::OtherNet);
  font24.draw(&c, (kW - tw) / 2, 628, ui::OtherNet, C_BLACK, C_WHITE);
  fullPush(c);
}

void doScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(80);
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
  gN = 0;
  for (int i = 0; i < n && gN < 16; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    snprintf(gNets[gN].ssid, sizeof(gNets[gN].ssid), "%s", s.c_str());
    gNets[gN].rssi = WiFi.RSSI(i);
    gNets[gN].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    gN++;
  }
}

bool hitKb(int x, int y) {
  int y0 = 188;
  if (y < y0 + 8) return false;
  for (int r = 0; r < 4; r++) {
    int rowY = y0 + 8 + r * 56;
    if (y < rowY || y >= rowY + 48) continue;
    const char* row = kRows[r];
    int n = (int)strlen(row);
    int cw = kW / n;
    int i = x / cw;
    if (i < 0 || i >= n) return true;
    if (gPassLen >= (int)sizeof(gPass) - 1) return true;
    char ch = row[i];
    if (gShift && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    gPass[gPassLen++] = ch;
    gPass[gPassLen] = 0;
    return true;
  }
  int by = y0 + 8 + 4 * 56;
  if (y >= by && y < by + 52) {
    if (x < 110) gShift = !gShift;
    else if (x < 340) {
      if (gPassLen < (int)sizeof(gPass) - 1) {
        gPass[gPassLen++] = ' ';
        gPass[gPassLen] = 0;
      }
    } else if (x < 430) {
      if (gPassLen > 0) gPass[--gPassLen] = 0;
    } else {
      return false;  // connect — handled by caller
    }
    return true;
  }
  return false;
}

void pumpSerialShot() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      line.trim();
      if (line == "shot") {
        Serial.println("[app] dump canvas start");
        render::dumpCanvas();
        Serial.println("[app] dump canvas done");
      }
      line = "";
    } else if (line.length() < 64) {
      line += ch;
    }
  }
}

bool tryConnect(M5Canvas& c, Config& cfg) {
  if (gSel < 0) return false;
  const char* pass = gNets[gSel].open ? "" : gPass;
  cfg.setWifi(gNets[gSel].ssid, pass);
  c.fillSprite(C_WHITE);
  font40.draw(&c, 36, 360, ui::Connecting, C_BLACK, C_WHITE);
  font24.drawEllipsis(&c, 36, 360 + kInk40 + 16, gNets[gSel].ssid, C_DARK, C_WHITE, kW - 72);
  fullPush(c);
  WiFi.disconnect(false, false);
  delay(200);
  bool ok = wifiConnect(cfg.ssid(), cfg.password(), 15000);
  if (ok) {
    c.fillSprite(C_WHITE);
    font40.draw(&c, 36, 380, ui::ConnectOk, C_BLACK, C_WHITE);
    fullPush(c);
    delay(800);
    return true;
  }
  gUi = BAD;
  drawFail(c);
  return false;
}

}  // namespace

bool wifiuiRun(Config& cfg, bool firstBoot) {
  M5Canvas* pc = render::canvas();
  if (!pc) return false;
  M5Canvas& c = *pc;
  gFirst = firstBoot;
  gSel = -1;
  gPass[0] = 0;
  gPassLen = 0;
  gShift = false;

  if (firstBoot) {
    gUi = WELCOME;
    drawWelcome(c);
  } else {
    gUi = LIST;
    drawScanning(c);
    doScan();
    drawList(c);
  }

  while (true) {
    M5.update();
    pumpSerialShot();
    auto t = M5.Touch.getDetail();
    if (!t.wasClicked()) {
      delay(30);
      continue;
    }
    int x = t.x, y = t.y;

    if (gUi == WELCOME) {
      if (y >= 670 && y < 780) {
        drawScanning(c);
        doScan();
        gUi = LIST;
        drawList(c);
      } else if (y >= 780 && y < 860) {
        return false;
      }
    } else if (gUi == LIST) {
      if (y < 56 && x < 160) return false;
      if (y < 56 && x > kW - 140) {
        drawScanning(c);
        doScan();
        drawList(c);
        continue;
      }
      if (y >= kListY) {
        int idx = (y - kListY) / 70;
        if (idx >= 0 && idx < gN && idx < 10) {
          gSel = idx;
          gPass[0] = 0;
          gPassLen = 0;
          if (gNets[gSel].open) {
            if (tryConnect(c, cfg)) return true;
          } else {
            gUi = PASS;
            drawPass(c);
          }
        }
      }
    } else if (gUi == PASS) {
      if (y < 50 && x < 140) {
        gUi = LIST;
        drawList(c);
        continue;
      }
      int by = 188 + 8 + 4 * 56;
      if (y >= by && y < by + 52 && x >= 430) {
        if (tryConnect(c, cfg)) return true;
        continue;
      }
      if (hitKb(x, y)) drawPass(c);
    } else if (gUi == BAD) {
      if (y >= 490 && y < 590) {
        gUi = PASS;
        drawPass(c);
      } else if (y >= 594 && y < 694) {
        gUi = LIST;
        drawList(c);
      }
    }
  }
}
