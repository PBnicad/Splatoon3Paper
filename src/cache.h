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
