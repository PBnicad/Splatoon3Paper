// LittleFS cache of the last good compact JSON (+ fetch metadata).

#pragma once

#include <cstddef>
#include <cstdint>
#include <WString.h>

bool cacheBegin();
bool cacheSaveCompact(const char* json, size_t len, const char* etag);
// Returns a PSRAM buffer with the cached JSON (caller frees), or nullptr.
char* cacheLoadCompact(size_t& len);
bool cacheLoadMeta(uint32_t& fetchedAt, String& etag);

// LittleFS is used by the UI task (read images) and the net task (write
// cache/images). Hold this around every LittleFS call after cacheBegin().
void fsLock();
void fsUnlock();
struct FsHold {
  FsHold() { fsLock(); }
  ~FsHold() { fsUnlock(); }
};
