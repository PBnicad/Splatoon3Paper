#include "net.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "certs.h"

static int caIndex = 0;  // remembered working root CA
static const char* const kCas[] = {CA_GTS_ROOT_R4, CA_ISRG_ROOT_X1};

bool wifiConnect(const char* ssid, const char* pass, uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // responsiveness over power (always-on desk mode)
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

void wifiKeepAlive() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}

int httpsGet(const char* host, const char* path, const char* etagIn,
             String& body, String& etagOut) {
  String url = String("https://") + host + path;
  for (int attempt = 0; attempt < 2; ++attempt) {
    WiFiClientSecure client;
    client.setCACert(kCas[caIndex]);
    client.setTimeout(15000);  // ms
    client.setHandshakeTimeout(15);
    HTTPClient http;
    if (!http.begin(client, url)) continue;
    http.setTimeout(15000);
    http.setUserAgent("M5Paper-Splatoon/1.0");
    if (etagIn && etagIn[0]) http.addHeader("If-None-Match", etagIn);
    int code = http.GET();
    if (code >= 0) {  // TLS handshake succeeded with this root
      if (code == 200) {
        body = http.getString();
        etagOut = http.header("ETag");
      }
      http.end();
      return code;
    }
    // transport error — likely wrong root CA after Cloudflare rotation
    Serial.printf("[net] TLS fail with CA#%d (%d), rotating\n", caIndex, code);
    caIndex = (caIndex + 1) % (sizeof(kCas) / sizeof(kCas[0]));
    http.end();
  }
  return -1;
}
