// WiFi + HTTPS access to the Cloudflare Worker.

#pragma once

#include <WiFiClientSecure.h>
#include <WString.h>

void netInit();  // PSRAM TLS allocators; call once after M5.begin()
bool wifiConnect(const char* ssid, const char* pass, uint32_t timeoutMs);
bool wifiConnected();
void wifiKeepAlive();

// HTTP GET https://host/path with pinned CA roots (both Cloudflare issuers
// are tried in turn; the working one is remembered).
// Returns HTTP status code (200 → body filled, 304 → unchanged), or a
// negative HTTPClient error code on transport failure.
int httpsGet(const char* host, const char* path, const char* etagIn,
             String& body, String& etagOut);

// Binary GET streamed to LittleFS. Returns HTTP status or a negative
// transport error. Yields the calling task while waiting on the socket so
// the UI task can keep running.
int httpsGetToFile(const char* host, const char* path, const char* fsPath);
