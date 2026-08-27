#include "img.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

#include "buddy_sni.h"
#include "cache.h"
#include "config.h"
#include "net.h"
#include "pix4.h"
#include "render.h"
#include "timekeeper.h"

static void fsName(const char* key, char* out, size_t cap) {
  // LittleFS name component max is 32. key is s:{64hex}_{0|1} — keep kind + 8 hex.
  const char* hash = (key[0] && key[1] == ':') ? key + 2 : key;
  snprintf(out, cap, "/img/%c_%.8s.sni", key[0], hash);
}

static bool keyOk(const char* key) {
  if (!key || !key[0] || strlen(key) > 70) return false;
  if (key[1] != ':') return false;
  char k = key[0];
  if (k != 's' && k != 'w' && k != 'g' && k != 'b') return false;
  return true;
}

static bool parseSniBuf(uint8_t* buf, size_t sz, uint16_t& w, uint16_t& h, uint8_t*& pix,
                        size_t& pixLen) {
  if (sz < 10 || memcmp(buf, "SNI1", 4) != 0) return false;
  memcpy(&w, buf + 4, 2);
  memcpy(&h, buf + 6, 2);
  if (w == 0 || h == 0 || w > 540 || h > 960) return false;
  pix = buf + 8;
  pixLen = sz - 8;
  return true;
}

static bool loadSni(const char* key, uint16_t& w, uint16_t& h, uint8_t*& pix, size_t& pixLen) {
  pix = nullptr;
  char path[96];
  fsName(key, path, sizeof(path));
  {
    FsHold hold;
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      if (f) {
        size_t sz = f.size();
        if (sz >= 10 && sz <= 80000) {
          uint8_t* buf = (uint8_t*)ps_malloc(sz);
          if (!buf) buf = (uint8_t*)malloc(sz);
          if (buf && f.read(buf, sz) == sz && parseSniBuf(buf, sz, w, h, pix, pixLen)) {
            f.close();
            return true;
          }
          free(buf);
        }
        f.close();
      }
    }
  }
  if (strcmp(key, kBuddyKey) != 0) return false;
  uint8_t* buf = (uint8_t*)ps_malloc(kBuddySniLen);
  if (!buf) buf = (uint8_t*)malloc(kBuddySniLen);
  if (!buf) return false;
  memcpy(buf, kBuddySni, kBuddySniLen);
  if (!parseSniBuf(buf, kBuddySniLen, w, h, pix, pixLen)) {
    free(buf);
    return false;
  }
  return true;
}

// Cover-crop blit: uniform scale so the source covers (dw,dh), then crop
// overflow. Never stretches independently on x/y.
static void blit(M5Canvas* dst, int x, int y, int dw, int dh,
                 const uint8_t* pix, uint16_t sw, uint16_t sh, size_t pixLen) {
  int srcX0 = 0, srcY0 = 0, srcW = sw, srcH = sh;
  if ((int32_t)dw * sh > (int32_t)dh * sw) {
    srcH = (int)((int32_t)sw * dh / dw);
    if (srcH < 1) srcH = 1;
    if (srcH > sh) srcH = sh;
    srcY0 = (sh - srcH) / 2;
  } else if ((int32_t)dh * sw > (int32_t)dw * sh) {
    srcW = (int)((int32_t)sh * dw / dh);
    if (srcW < 1) srcW = 1;
    if (srcW > sw) srcW = sw;
    srcX0 = (sw - srcW) / 2;
  }
  pix4::Acc acc;
  bool fast = pix4::init(dst, acc);
  for (int dy = 0; dy < dh; dy++) {
    int sy = srcY0 + (int)((int32_t)dy * srcH / dh);
    if (sy < 0) sy = 0;
    if (sy >= sh) sy = sh - 1;
    for (int dx = 0; dx < dw; dx++) {
      int sx = srcX0 + (int)((int32_t)dx * srcW / dw);
      if (sx < 0) sx = 0;
      if (sx >= sw) sx = sw - 1;
      int i = sy * sw + sx;
      if ((size_t)(i >> 1) >= pixLen) continue;
      uint8_t byte = pix[i >> 1];
      uint8_t n = (i & 1) ? (byte & 0x0F) : (byte >> 4);
      if (fast) pix4::put(acc, x + dx, y + dy, n);
      else dst->drawPixel(x + dx, y + dy, n);
    }
  }
}

bool imgDrawFit(M5Canvas* dst, int x, int y, int dw, int dh, const char* key) {
  if (!dst || !keyOk(key) || dw <= 0 || dh <= 0) return false;
  uint16_t sw, sh;
  uint8_t* pix;
  size_t pixLen;
  if (!loadSni(key, sw, sh, pix, pixLen)) return false;
  uint8_t* base = pix - 8;
  blit(dst, x, y, dw, dh, pix, sw, sh, pixLen);
  free(base);
  return true;
}

bool imgDraw(M5Canvas* dst, int x, int y, const char* key) {
  if (!dst || !keyOk(key)) return false;
  uint16_t sw, sh;
  uint8_t* pix;
  size_t pixLen;
  if (!loadSni(key, sw, sh, pix, pixLen)) return false;
  uint8_t* base = pix - 8;
  blit(dst, x, y, sw, sh, pix, sw, sh, pixLen);
  free(base);
  return true;
}

bool imgDrawContain(M5Canvas* dst, int x, int y, int boxW, int boxH, const char* key) {
  if (!dst || !keyOk(key) || boxW <= 0 || boxH <= 0) return false;
  uint16_t sw, sh;
  uint8_t* pix;
  size_t pixLen;
  if (!loadSni(key, sw, sh, pix, pixLen)) return false;
  uint8_t* base = pix - 8;
  int dw, dh;
  if ((int32_t)boxW * sh < (int32_t)boxH * sw) {
    dw = boxW;
    dh = (int)((int32_t)boxW * sh / sw);
    if (dh < 1) dh = 1;
  } else {
    dh = boxH;
    dw = (int)((int32_t)boxH * sw / sh);
    if (dw < 1) dw = 1;
  }
  int ox = x + (boxW - dw) / 2;
  int oy = y + (boxH - dh) / 2;
  blit(dst, ox, oy, dw, dh, pix, sw, sh, pixLen);
  free(base);
  return true;
}

int imgPrefetchKey(const char* key) {
  if (!keyOk(key) || !wifiConnected()) return -1;
  char path[96];
  fsName(key, path, sizeof(path));
  {
    FsHold hold;
    if (LittleFS.exists(path)) return 200;
    if (!LittleFS.exists("/img")) LittleFS.mkdir("/img");
  }
  char enc[96];
  size_t n = 0;
  for (const char* p = key; *p && n + 4 < sizeof(enc); ++p) {
    if (*p == ':') {
      memcpy(enc + n, "%3A", 3);
      n += 3;
    } else {
      enc[n++] = *p;
    }
  }
  enc[n] = 0;
  char urlpath[128];
  snprintf(urlpath, sizeof(urlpath), "/api/v1/img?k=%s", enc);
  int code = httpsGetToFile(kApiHost, urlpath, path);
  Serial.printf("[img] %s -> %d\n", key, code);
  if (code != 200) {
    FsHold hold;
    LittleFS.remove(path);
  }
  return code;
}

// The queue stores key copies, not pointers into Model: the UI task can
// rebuild the queue (page turn) while the net task is mid-download, and the
// model itself is replaced under the state lock after each successful fetch.
static char gQueue[64][80];
static int gQn = 0, gQi = 0;

static void addKey(const char* k) {
  if (!k || !k[0] || gQn >= 64) return;
  for (int i = 0; i < gQn; i++)
    if (!strcmp(gQueue[i], k)) return;
  snprintf(gQueue[gQn], sizeof(gQueue[gQn]), "%s", k);
  ++gQn;
}

static void addSlot(const Slot& s) {
  addKey(s.si1);
  addKey(s.si2);
}

void imgQueue(const Model& m) {
  gQn = 0;
  gQi = 0;
  // Visible-now first: current slots, current salmon, gear, events.
  for (int i = 0; i < m.nModes; i++) {
    if (m.modes[i].hasA) addSlot(m.modes[i].a);
  }
  if (m.nShifts > 0) {
    addKey(m.shifts[0].si);
    for (int w = 0; w < 4; w++) addKey(m.shifts[0].wi[w]);
  }
  for (int i = 0; i < m.nGear && i < 6; i++) addKey(m.gear[i].img);
  for (int i = 0; i < m.nEvents && i < 2; i++) {
    addKey(m.events[i].si1);
    addKey(m.events[i].si2);
  }
  // Then one upcoming slot per mode, then remaining salmon.
  for (int i = 0; i < m.nModes; i++) {
    if (m.modes[i].nu > 0) addSlot(m.modes[i].u[0]);
  }
  for (int i = 1; i < m.nShifts && i < 4; i++) {
    addKey(m.shifts[i].si);
    for (int w = 0; w < 4; w++) addKey(m.shifts[i].wi[w]);
  }
  Serial.printf("[img] queued %d\n", gQn);
}

static void addModeVisible(const ModeSlots* mm, int maxSlots) {
  if (!mm) return;
  uint32_t now = nowEpoch();
  int added = 0;
  auto take = [&](const Slot& s) {
    if (!s.st || s.et <= now || added >= maxSlots) return;
    addSlot(s);
    ++added;
  };
  if (mm->hasA) take(mm->a);
  for (int i = 0; i < mm->nu; ++i) take(mm->u[i]);
}

void imgQueuePage(const Model& m, int page) {
  gQn = 0;
  gQi = 0;
  uint32_t now = nowEpoch();
  switch (page) {
    case render::kPageRegular:
      addModeVisible(m.findMode(ui::ModeRegular), 3);
      break;
    case render::kPageAnarchy:
      addModeVisible(m.findMode(ui::ModeSeries), 2);
      addModeVisible(m.findMode(ui::ModeOpen), 2);
      break;
    case render::kPageX:
      addModeVisible(m.findMode(ui::ModeX), 3);
      break;
    case render::kPageFest:
      addModeVisible(m.findMode(ui::ModeFestOpen), 2);
      addModeVisible(m.findMode(ui::ModeFestPro), 2);
      break;
    case render::kPageEvents:
      for (int i = 0; i < m.nEvents && i < 2; i++) {
        addKey(m.events[i].si1);
        addKey(m.events[i].si2);
      }
      break;
    case render::kPageSalmon: {
      int i0 = m.liveShiftIndex(now);
      if (i0 < 0) i0 = 0;
      for (int i = i0; i < m.nShifts && i < i0 + 3; i++) {
        addKey(m.shifts[i].si);
        if (i == i0) {
          for (int w = 0; w < 4; w++) addKey(m.shifts[i].wi[w]);
        }
      }
      break;
    }
    case render::kPageGear:
      for (int i = 0; i < m.nGear && i < 6; i++) addKey(m.gear[i].img);
      break;
    default:
      break;
  }
  Serial.printf("[img] page %d queued %d\n", page, gQn);
}

bool imgPending() {
  while (gQi < gQn) {
    char path[96];
    fsName(gQueue[gQi], path, sizeof(path));
    FsHold hold;
    if (!LittleFS.exists(path)) return true;
    ++gQi;
  }
  return false;
}

static bool gNeedPaint = false;

bool imgPump() {
  while (gQi < gQn) {
    const char* k = gQueue[gQi];
    char path[32];
    fsName(k, path, sizeof(path));
    {
      FsHold hold;
      if (LittleFS.exists(path)) {
        ++gQi;
        continue;
      }
    }
    int code = imgPrefetchKey(k);
    ++gQi;
    if (code == 200) gNeedPaint = true;
    return gQi < gQn;
  }
  return false;
}

bool imgJustFinished() {
  if (!gNeedPaint) return false;
  gNeedPaint = false;
  return true;
}
