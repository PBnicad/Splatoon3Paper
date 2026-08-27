#include "net.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include "ssl_client.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "cache.h"
#include "certs.h"
#include "timekeeper.h"

// Handshake wants two 16KB record buffers. Internal DRAM is too tight once
// Wi-Fi + the net task are up, so mbedtls allocs go to PSRAM first.
static void* tlsCalloc(size_t n, size_t sz) {
  void* p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

void netInit() {
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
}

// Arduino-ESP32 2.x WiFiClientSecure::read() returns -1 when available()==0,
// and available() only counts already-decrypted bytes. Cloudflare sends ~8KB
// TLS records, so a 9KB body stalls after the first record. Pull the next
// record with mbedtls_ssl_read directly.
class TlsClient : public WiFiClientSecure {
 public:
  int read(uint8_t* buf, size_t size) override {
    if (!buf || !size || !sslclient) return -1;
    int peeked = 0;
    if (_peek >= 0) {
      buf[0] = (uint8_t)_peek;
      _peek = -1;
      peeked = 1;
      ++buf;
      --size;
      if (!size) return 1;
    }
    int r = mbedtls_ssl_read(&sslclient->ssl_ctx, buf, size);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
      return peeked ? peeked : -1;
    }
    if (r < 0) return peeked ? peeked : r;
    return r + peeked;
  }
};

// mbedTLS parses every PEM in the buffer, so both Cloudflare issuers
// (Google Trust Services + Let's Encrypt) are accepted without rotating.
static const char* caBundle() {
  static String bundle;
  if (!bundle.length()) {
    bundle.reserve(sizeof(CA_GTS_ROOT_R4) + sizeof(CA_ISRG_ROOT_X1));
    bundle += CA_GTS_ROOT_R4;
    bundle += CA_ISRG_ROOT_X1;
  }
  return bundle.c_str();
}

bool wifiConnect(const char* ssid, const char* pass, uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // TLS/HTTP reliability while the radio is up
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    if (millis() - t0 > timeoutMs) return false;
  }
  Serial.printf("[net] wifi connected, ip=%s\n", WiFi.localIP().toString().c_str());
  return true;
}

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void wifiRadioOff() {
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  Serial.println("[net] radio off");
}

// When NTP is blocked and the RTC battery is dead, the HTTP Date response
// header (present on every Cloudflare response, ~1s resolution) is the only
// clock source left. Seed the system clock once; NTP still polishes it later.
static uint32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)era * 146097 + doe - 719468;
}

static void seedTimeFromHttpDate(const String& d) {
  if (timeValid()) return;
  static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char wd[8], mon[4] = {0};
  int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
  // "Tue, 15 Nov 2024 08:12:31 GMT"
  if (sscanf(d.c_str(), "%3[^,], %d %3s %d %d:%d:%d", wd, &day, mon, &year, &hh,
             &mm, &ss) != 7) {
    return;
  }
  int mo = -1;
  for (int i = 0; i < 12; ++i)
    if (!strcmp(mon, kMonths[i])) mo = i + 1;
  if (mo < 1 || year < 2023 || year > 2100) return;
  time_t epoch = (time_t)daysFromCivil(year, mo, day) * 86400 + hh * 3600 + mm * 60 + ss;
  if (epoch <= 1700000000) return;
  struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  Serial.println("[net] clock seeded from HTTP Date");
}

static bool readHttpBody(HTTPClient& http, String& body, uint32_t timeoutMs) {
  int len = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  if (!stream) return false;
  body = "";
  if (len > 200000) {
    Serial.printf("[net] body too large: %d\n", len);
    return false;
  }
  if (len > 0 && !body.reserve(len + 1)) {
    Serial.printf("[net] reserve failed len=%d heap=%u\n", len, ESP.getFreeHeap());
    return false;
  }
  uint32_t t0 = millis();
  int got = 0;
  while (len <= 0 || got < len) {
    if (millis() - t0 > timeoutMs) {
      Serial.printf("[net] body timeout got=%d want=%d\n", got, len);
      break;
    }
    if (len <= 0 && !http.connected() && !stream->available()) break;
    char tmp[512];
    int nwant = (int)sizeof(tmp);
    if (len > 0 && got + nwant > len) nwant = len - got;
    int n = stream->read((uint8_t*)tmp, nwant);
    if (n > 0) {
      body.concat(tmp, (unsigned)n);
      got += n;
      continue;
    }
    if (len <= 0) break;
    vTaskDelay(1);
  }
  return body.length() > 0;
}

int httpsGet(const char* host, const char* path, const char* etagIn,
             String& body, String& etagOut) {
  String url = String("https://") + host + path;
  Serial.printf("[net] httpsGet %s heap=%u intern=%u psram=%u\n", path, ESP.getFreeHeap(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (attempt) delay(400);
    TlsClient client;
    client.setCACert(caBundle());
    client.setTimeout(20);           // WiFiClientSecure: seconds
    client.setHandshakeTimeout(30);  // seconds
    HTTPClient http;
    if (!http.begin(client, url)) continue;
    http.setTimeout(20000);  // HTTPClient: milliseconds
    http.useHTTP10(true);    // Content-Length, no chunked (Cloudflare default)
    http.setReuse(false);
    http.setUserAgent("M5Paper-Splatoon/1.0");
    const char* hdrs[] = {"ETag", "X-Cache", "Content-Length", "Transfer-Encoding", "Date"};
    http.collectHeaders(hdrs, 5);
    if (etagIn && etagIn[0]) http.addHeader("If-None-Match", etagIn);
    int code = http.GET();
    int sz = http.getSize();
    seedTimeFromHttpDate(http.header("Date"));
    Serial.printf("[net] GET done code=%d size=%d te=%s heap=%u try=%d\n", code, sz,
                  http.header("Transfer-Encoding").c_str(), ESP.getFreeHeap(), attempt);
    Serial.flush();
    if (code >= 0) {
      if (code == 200) {
        if (!readHttpBody(http, body, 20000)) {
          Serial.printf("[net] body read failed heap=%u\n", ESP.getFreeHeap());
        }
        etagOut = http.header("ETag");
        Serial.printf("[net] body=%u etag=%s\n", (unsigned)body.length(),
                      etagOut.c_str());
      }
      http.end();
      return code;
    }
    char errbuf[96] = {0};
    int mbed = client.lastError(errbuf, sizeof(errbuf));
    Serial.printf("[net] TLS fail http=%d mbedtls=%d %s try=%d\n", code, mbed, errbuf,
                  attempt);
    http.end();
  }
  return -1;
}

int httpsGetToFile(const char* host, const char* path, const char* fsPath) {
  String url = String("https://") + host + path;
  Serial.printf("[net] httpsGetToFile %s\n", path);
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (attempt) delay(400);
    TlsClient client;
    client.setCACert(caBundle());
    client.setTimeout(20);
    client.setHandshakeTimeout(30);
    HTTPClient http;
    if (!http.begin(client, url)) continue;
    http.setTimeout(30000);
    http.useHTTP10(true);
    http.setReuse(false);
    http.setUserAgent("M5Paper-Splatoon/1.0");
    int code = http.GET();
    int sz = http.getSize();
    Serial.printf("[net] file GET code=%d size=%d try=%d\n", code, sz, attempt);
    if (code != 200) {
      http.end();
      if (code >= 0) return code;
      continue;
    }
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
      http.end();
      return -2;
    }
    File f;
    {
      FsHold hold;
      f = LittleFS.open(fsPath, "w");
      if (!f) {
        http.end();
        return -2;
      }
    }
    uint32_t t0 = millis();
    uint32_t last = t0;
    int got = 0;
    uint8_t buf[1024];
    while (sz <= 0 || got < sz) {
      if (millis() - t0 > 25000) break;
      if (millis() - last > 8000) break;
      int nwant = (int)sizeof(buf);
      if (sz > 0 && got + nwant > sz) nwant = sz - got;
      int n = stream->read(buf, nwant);
      if (n > 0) {
        FsHold hold;
        f.write(buf, n);
        got += n;
        last = millis();
        continue;
      }
      if (sz <= 0) break;
      vTaskDelay(1);
    }
    {
      FsHold hold;
      f.close();
    }
    http.end();
    if (sz > 0 && got != sz) {
      FsHold hold;
      LittleFS.remove(fsPath);
      Serial.printf("[net] file short %d/%d\n", got, sz);
      continue;
    }
    if (got < 8) {
      FsHold hold;
      LittleFS.remove(fsPath);
      continue;
    }
    return 200;
  }
  return -1;
}
