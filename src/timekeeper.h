// System clock management: NTP sync, BM8563 RTC backup, UTC+8 formatting.
// Data epochs are UTC seconds; all display is China Standard Time (UTC+8).

#pragma once

#include <cstdint>
#include <cstddef>

bool timeValid();
uint32_t nowEpoch();
// NTP sync (blocks up to timeoutMs); poll until the system clock is valid.
bool timeSyncNtp(uint32_t timeoutMs);
// Mirror a valid system clock into the BM8563 (stored as UTC). UI task only —
// it shares the I2C bus with the touch controller.
void timeMirrorToRtc();
// On boot: if system time is unset, seed it from the RTC.
void timeSeedFromRtc();

void fmtClock(uint32_t epoch, char* out, size_t n);        // "12:34"
void fmtDateWeek(uint32_t epoch, char* out, size_t n);     // "8月20日 周四"
void fmtHM(uint32_t epoch, char* out, size_t n);           // "8/21 08:00"
void fmtRange(uint32_t st, uint32_t et, char* out, size_t n);  // "08:00-10:00" (同日) / "8/21 08:00-8/23 00:00"
void fmtCountdown(uint32_t secsLeft, char* out, size_t n); // "2天3小时" / "45分" / "12秒"
