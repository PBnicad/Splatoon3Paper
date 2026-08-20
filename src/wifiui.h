#pragma once

#include "config.h"

// Blocking on-screen Wi-Fi setup (scan + keyboard).
// firstBoot: welcome → scan; 稍后再说 leaves without saving.
// Returns true if credentials were saved and the association succeeded.
bool wifiuiRun(Config& cfg, bool firstBoot = false);
