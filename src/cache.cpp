#include "cache.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>

static const char* kCompactPath = "/compact.json";
static const char* kMetaPath = "/meta.json";

bool cacheBegin() {
  if (LittleFS.begin()) return true;
  Serial.println("[cache] format + retry");
  LittleFS.format();
  return LittleFS.begin(true);
}

bool cacheSaveCompact(const char* json, size_t len, const char* etag) {
  File f = LittleFS.open(kCompactPath, "w");
  if (!f) return false;
  size_t written = f.write((const uint8_t*)json, len);
  f.close();
  if (written != len) return false;

  File m = LittleFS.open(kMetaPath, "w");
  if (m) {
    m.printf("{\"ts\":%lu,\"etag\":\"%s\"}",
             (unsigned long)time(nullptr), etag ? etag : "");
    m.close();
  }
  return true;
}

char* cacheLoadCompact(size_t& len) {
  if (!LittleFS.exists(kCompactPath)) return nullptr;
  File f = LittleFS.open(kCompactPath, "r");
  if (!f) return nullptr;
  len = f.size();
  char* buf = (char*)ps_malloc(len + 1);
  if (!buf) { f.close(); return nullptr; }
  size_t got = f.read((uint8_t*)buf, len);
  f.close();
  if (got != len) { free(buf); return nullptr; }
  buf[len] = 0;
  return buf;
}

bool cacheLoadMeta(uint32_t& fetchedAt, String& etag) {
  fetchedAt = 0;
  etag = "";
  if (!LittleFS.exists(kMetaPath)) return false;
  File f = LittleFS.open(kMetaPath, "r");
  if (!f) return false;
  String s = f.readString();
  f.close();
  int ts = s.indexOf("\"ts\":");
  int et = s.indexOf("\"etag\":\"");
  if (ts >= 0) fetchedAt = strtoul(s.c_str() + ts + 5, nullptr, 10);
  if (et >= 0) {
    int start = et + 8;
    int end = s.indexOf('"', start);
    if (end > start) etag = s.substring(start, end);
  }
  return fetchedAt > 0;
}
