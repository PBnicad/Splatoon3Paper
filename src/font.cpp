#include "font.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "pix4.h"

FontFace font24, font40, font96;

uint32_t utf8Next(const char*& p) {
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  int extra;
  uint32_t cp;
  if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
  else return 0xFFFD;
  for (int i = 0; i < extra; ++i) {
    uint8_t cc = (uint8_t)*p;
    if ((cc & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (cc & 0x3F);
    ++p;
  }
  return cp;
}

int utf8Len(const char* s) {
  int n = 0;
  while (*s) { utf8Next(s); ++n; }
  return n;
}

void utf8Truncate(char* dst, size_t dstSize, const char* src, int maxChars) {
  const char* p = src;
  char* out = dst;
  char* end = dst + dstSize - 1;
  int n = 0;
  const char* lastStart = p;
  while (*p && n < maxChars && out < end) {
    lastStart = p;
    uint32_t cp = utf8Next(p);
    size_t len = (size_t)(p - lastStart);
    if ((size_t)(end - out) < len) break;
    memcpy(out, lastStart, len);
    out += len;
    ++n;
    (void)cp;
  }
  *out = 0;
}

FontFace::~FontFace() {
  if (buf_) free(buf_);
}

bool FontFace::load(const char* path) {
  if (buf_) { free(buf_); buf_ = nullptr; }
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_ = f.size();
  buf_ = (uint8_t*)ps_malloc(size_);
  if (!buf_) buf_ = (uint8_t*)malloc(size_);
  if (!buf_) { f.close(); return false; }
  if (f.read(buf_, size_) != size_) { f.close(); free(buf_); buf_ = nullptr; return false; }
  f.close();
  if (size_ < 24 || memcmp(buf_, "SNF1", 4) != 0) { free(buf_); buf_ = nullptr; return false; }
  uint16_t ver;
  memcpy(&ver, buf_ + 4, 2);
  memcpy(&px_, buf_ + 6, 2);
  memcpy(&gap_, buf_ + 8, 2);
  memcpy(&count_, buf_ + 12, 4);
  memcpy(&indexOff_, buf_ + 16, 4);
  memcpy(&dataOff_, buf_ + 20, 4);
  if (ver != 1 || count_ == 0 || indexOff_ < 24 || dataOff_ < indexOff_ + 8LL * count_ ||
      dataOff_ > size_) {
    free(buf_); buf_ = nullptr; return false;
  }
  return true;
}

const FontFace::GlyphHead* FontFace::find(uint32_t cp) const {
  uint32_t lo = 0, hi = count_;
  while (lo < hi) {
    uint32_t mid = (lo + hi) / 2;
    uint32_t key, off;
    const uint8_t* e = buf_ + indexOff_ + 8LL * mid;
    memcpy(&key, e, 4);
    memcpy(&off, e + 4, 4);
    if (key == cp) {
      return (const GlyphHead*)(buf_ + dataOff_ + off);
    }
    if (key < cp) lo = mid + 1; else hi = mid;
  }
  return nullptr;
}

int FontFace::textWidth(const char* utf8) const {
  if (!valid()) return 0;
  int w = 0;
  const char* p = utf8;
  while (*p) {
    uint32_t cp = utf8Next(p);
    const GlyphHead* g = find(cp);
    if (g) {
      w += g->adv;
    } else if ((g = find('?'))) {
      w += g->adv;
    }
  }
  return w;
}

int FontFace::draw(M5Canvas* dst, int x, int y, const char* utf8,
                   uint8_t fg, uint8_t bg) const {
  if (!valid()) return 0;
  pix4::Acc acc;
  bool fast = pix4::init(dst, acc);
  int pen = x;
  const char* p = utf8;
  while (*p) {
    uint32_t cp = utf8Next(p);
    const GlyphHead* g = find(cp);
    if (!g) g = find('?');
    if (!g) { pen += px_ / 2; continue; }
    const uint8_t* alpha = (const uint8_t*)g + sizeof(GlyphHead);
    int gw = g->w, gh = g->h;
    int gx0 = pen + g->xoff, gy0 = y + g->yoff;
    for (int gy = 0; gy < gh; ++gy) {
      for (int gx = 0; gx < gw; ++gx) {
        int i = gy * gw + gx;
        uint8_t byte = alpha[i >> 1];
        uint8_t a = (i & 1) ? (byte & 0x0F) : (byte >> 4);
        if (!a) continue;
        uint8_t c = (uint8_t)((fg * a + bg * (15 - a) + 7) / 15);
        if (fast) pix4::put(acc, gx0 + gx, gy0 + gy, c);
        else dst->drawPixel(gx0 + gx, gy0 + gy, c);
      }
    }
    pen += g->adv;
  }
  return pen - x;
}

int FontFace::drawEllipsis(M5Canvas* dst, int x, int y, const char* utf8,
                           uint8_t fg, uint8_t bg, int maxWidth) const {
  if (textWidth(utf8) <= maxWidth) return draw(dst, x, y, utf8, fg, bg);
  int ell = textWidth("…");
  char tmp[128];
  int keep = utf8Len(utf8);
  int lo = 0, hi = keep;
  while (lo < hi) {  // largest prefix that fits together with "…"
    int mid = (lo + hi + 1) / 2;
    utf8Truncate(tmp, sizeof(tmp), utf8, mid);
    if (textWidth(tmp) + ell <= maxWidth) lo = mid; else hi = mid - 1;
  }
  utf8Truncate(tmp, sizeof(tmp), utf8, lo);
  draw(dst, x, y, tmp, fg, bg);
  const char* e = "…";
  draw(dst, x + textWidth(tmp), y, e, fg, bg);
  return maxWidth;
}

bool fontSetup() {
  bool ok = font24.load("/font24.bin");
  font40.load("/font40.bin");
  font96.load("/font96.bin");
  Serial.printf("[font] 24:%s 40:%s 96:%s\n",
                font24.valid() ? "ok" : "MISSING",
                font40.valid() ? "ok" : "MISSING",
                font96.valid() ? "ok" : "MISSING");
  return ok;
}
