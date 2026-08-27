// Direct-write fast path for 4bpp M5Canvas sprites.
//
// Font glyph and image blits used to go through drawPixel(), which costs a
// virtual dispatch + clip + palette check per pixel — several hundred
// thousand calls per full page. Sprites created with setColorDepth(4) keep a
// tightly packed buffer (2 pixels per byte, high nibble first, stride
// (width+1)/2 — the same layout dumpCanvas() decodes), so bulk pixel writes
// can go straight to the buffer. Falls back to drawPixel for anything that is
// not a plain 4bpp sprite.

#pragma once

#include <M5GFX.h>
#include <stdint.h>

namespace pix4 {

struct Acc {
  uint8_t* buf;
  int32_t w, h;
  uint32_t stride;
};

inline bool init(M5Canvas* c, Acc& a) {
  if (!c || c->getColorDepth() != 4) return false;
  a.buf = (uint8_t*)c->getBuffer();
  a.w = c->width();
  a.h = c->height();
  if (!a.buf || a.w < 1 || a.h < 1) return false;
  // Panel_Sprite pads _bitwidth up to a pixel-pair boundary; bufferLength()
  // exposes the true allocation, so derive the stride instead of guessing
  // (it equals width/2 for the even widths used here, but stays correct
  // for odd widths too).
  a.stride = c->bufferLength() / a.h;
  return a.stride >= (uint32_t)(a.w + 1) / 2;
}

inline void put(const Acc& a, int x, int y, uint8_t n) {
  if ((uint32_t)x >= (uint32_t)a.w || (uint32_t)y >= (uint32_t)a.h) return;
  uint8_t* p = a.buf + (uint32_t)y * a.stride + ((uint32_t)x >> 1);
  if (x & 1) {
    *p = (*p & 0xF0) | (n & 0x0F);
  } else {
    *p = (*p & 0x0F) | (uint8_t)(n << 4);
  }
}

}  // namespace pix4
