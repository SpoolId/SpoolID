#pragma once
#include <Arduino.h>

// Connects to the configured WiFi (STA). If that fails within a timeout, starts a
// fallback access point and a captive-portal DNS so any hostname resolves to the
// device, letting the user reach the config page to fix credentials.
namespace net {

enum class Mode { Connecting, Station, AccessPoint };

void begin();        // read config, try STA, fall back to AP
void loop();         // service captive-portal DNS (no-op in STA mode)
Mode mode();
String ip();         // current IP (STA or AP)
bool isAp();

}  // namespace net
