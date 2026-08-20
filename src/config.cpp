#include "config.h"

void Config::begin() {
  prefs_.begin("splatoon", false);
  prefs_.getString("ssid", ssid_, sizeof(ssid_));
  prefs_.getString("pass", pass_, sizeof(pass_));
  autoFetch_ = prefs_.getBool("autofetch", true);
  onboarded_ = prefs_.getBool("onboarded", false);
}

void Config::setWifi(const char* ssid, const char* pass) {
  snprintf(ssid_, sizeof(ssid_), "%s", ssid ? ssid : "");
  snprintf(pass_, sizeof(pass_), "%s", pass ? pass : "");
  prefs_.putString("ssid", ssid_);
  prefs_.putString("pass", pass_);
}

void Config::factoryReset() {
  ssid_[0] = 0;
  pass_[0] = 0;
  autoFetch_ = true;
  onboarded_ = false;
  prefs_.clear();
}
