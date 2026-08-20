#pragma once

#include <M5GFX.h>
#include "model.h"

// SNI1 4bpp grayscale blobs produced by the Worker (/api/v1/img?k=).
bool imgDraw(M5Canvas* dst, int x, int y, const char* key);
bool imgDrawFit(M5Canvas* dst, int x, int y, int dw, int dh, const char* key);
// Uniform scale to fit inside (boxW,boxH), centered; never stretches.
bool imgDrawContain(M5Canvas* dst, int x, int y, int boxW, int boxH, const char* key);
// Download any missing images used by the current model. Safe to call often.
void imgQueue(const Model& m);
bool imgPump();  // one download attempt; true if queue still has items
bool imgJustFinished();  // true once after new files landed
int imgPrefetchKey(const char* key);

static constexpr const char* kBuddyKey = "b:buddy2";
