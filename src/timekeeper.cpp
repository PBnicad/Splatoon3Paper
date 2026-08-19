#include "timekeeper.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <time.h>

static const char* kNtpServers[] = {"ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org"};
static constexpr int kTzOffsetSec = 8 * 3600;

bool timeValid() {
  return time(nullptr) > 1700000000;  // post 2023-11
}

uint32_t nowEpoch() {
  return (uint32_t)time(nullptr);
}

bool timeSyncNtp(uint32_t timeoutMs) {
  configTzTime("CST-8", kNtpServers[0], kNtpServers[1], kNtpServers[2]);
  uint32_t t0 = millis();
  while (!timeValid()) {
    delay(200);
    if (millis() - t0 > timeoutMs) return false;
  }
  // mirror into the RTC (stored as UTC)
  time_t t = time(nullptr);
  struct tm utc;
  gmtime_r(&t, &utc);
  m5::rtc_date_t d;
  d.year = utc.tm_year + 1900;
  d.month = utc.tm_mon + 1;
  d.date = utc.tm_mday;
  d.weekDay = utc.tm_wday;
  m5::rtc_time_t tmv;
  tmv.hours = utc.tm_hour;
  tmv.minutes = utc.tm_min;
  tmv.seconds = utc.tm_sec;
  M5.Rtc.setDate(&d);
  M5.Rtc.setTime(&tmv);
  Serial.println("[time] ntp synced, rtc updated");
  return true;
}

void timeSeedFromRtc() {
  if (timeValid()) return;
  m5::rtc_date_t d;
  m5::rtc_time_t t;
  if (!M5.Rtc.getDate(&d) || !M5.Rtc.getTime(&t)) return;
  if (d.year < 2024) return;
  struct tm utc = {};
  utc.tm_year = d.year - 1900;
  utc.tm_mon = d.month - 1;
  utc.tm_mday = d.date;
  utc.tm_hour = t.hours;
  utc.tm_min = t.minutes;
  utc.tm_sec = t.seconds;
  time_t epoch = mktime(&utc) - kTzOffsetSec;  // mktime is local (CST-8)
  struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  Serial.println("[time] seeded from rtc");
}

static void localParts(uint32_t epoch, struct tm& out) {
  time_t t = epoch;
  localtime_r(&t, &out);
}

void fmtClock(uint32_t epoch, char* out, size_t n) {
  struct tm lt;
  localParts(epoch, lt);
  snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

void fmtHM(uint32_t epoch, char* out, size_t n) {
  struct tm lt;
  localParts(epoch, lt);
  snprintf(out, n, "%d/%d %02d:%02d", lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
}

void fmtRange(uint32_t st, uint32_t et, char* out, size_t n) {
  struct tm a, b;
  localParts(st, a);
  localParts(et, b);
  if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) {
    snprintf(out, n, "%02d:%02d-%02d:%02d", a.tm_hour, a.tm_min, b.tm_hour, b.tm_min);
  } else {
    char aBuf[24], bBuf[24];
    fmtHM(st, aBuf, sizeof(aBuf));
    fmtHM(et, bBuf, sizeof(bBuf));
    snprintf(out, n, "%s - %s", aBuf, bBuf);
  }
}

void fmtCountdown(uint32_t secsLeft, char* out, size_t n) {
  if (secsLeft > 86400UL * 2) {
    snprintf(out, n, "%u%s", secsLeft / 86400, "天");
  } else if (secsLeft >= 86400UL) {
    snprintf(out, n, "%u%s%u%s", secsLeft / 86400, "天", (secsLeft % 86400) / 3600, "小时");
  } else if (secsLeft >= 3600UL) {
    snprintf(out, n, "%u%s%u%s", secsLeft / 3600, "小时", (secsLeft % 3600) / 60, "分");
  } else if (secsLeft >= 60UL) {
    snprintf(out, n, "%u%s", secsLeft / 60, "分");
  } else {
    snprintf(out, n, "%u%s", secsLeft, "秒");
  }
}
