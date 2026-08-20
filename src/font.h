// SNF1 bitmap font renderer (4bpp grayscale glyphs, see tools/make_font.py).
// Fonts live in LittleFS as /font24.bin /font40.bin /font96.bin and are
// loaded whole into PSRAM on boot.

#pragma once

#include <cstdint>
#include <M5GFX.h>

class FontFace {
 public:
  ~FontFace();

  bool load(const char* littlefsPath);
  bool valid() const { return buf_ != nullptr; }
  int pixelSize() const { return px_; }
  int lineHeight() const { return px_ + gap_ + 2; }

  int textWidth(const char* utf8) const;
  // draw with (x, y) = top-left of the line box; returns total advance
  int draw(M5Canvas* dst, int x, int y, const char* utf8,
           uint8_t fg, uint8_t bg) const;
  // draw, truncating with "…" when wider than maxWidth
  int drawEllipsis(M5Canvas* dst, int x, int y, const char* utf8,
                   uint8_t fg, uint8_t bg, int maxWidth) const;

 private:
  // SNF1 glyph: u8 w,h | i8 xoff,yoff | u8 adv,flags | u8 r0,r1  then 4bpp bitmap.
  // The two reserved bytes are part of the on-disk header (see tools/make_font.py);
  // treating the struct as 6 bytes shifts every glyph's bitmap and garbles text.
  struct GlyphHead {
    uint8_t w, h;
    int8_t xoff, yoff;
    uint8_t adv, flags;
    uint8_t r0, r1;
  } __attribute__((packed));
  static_assert(sizeof(GlyphHead) == 8, "SNF1 glyph header is 8 bytes");
  const GlyphHead* find(uint32_t cp) const;

  uint8_t* buf_ = nullptr;   // PSRAM
  size_t size_ = 0;
  uint16_t px_ = 0, gap_ = 0;
  uint32_t count_ = 0, indexOff_ = 0, dataOff_ = 0;
};

// decode one UTF-8 codepoint at *p, advance p; returns U+FFFD on error
uint32_t utf8Next(const char*& p);
int utf8Len(const char* s);
void utf8Truncate(char* dst, size_t dstSize, const char* src, int maxChars);

// global font instances (loaded by fontSetup())
extern FontFace font24, font40, font96;
bool fontSetup();  // returns true when at least the body font loaded
