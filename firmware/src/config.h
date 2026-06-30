#pragma once
#include <Arduino.h>

#include "logger.h"

// Persisted device settings (NVS via Preferences). Defaults come from secrets.h
// on first boot; the config web/serial pages overwrite them at runtime.
struct Config {
  String wifiSsid;
  String wifiPass;
  String apSsid;     // SSID of the fallback access point
  String apPass;     // AP password ("" => open network)
  String hostname;   // mDNS hostname (-> <hostname>.local)
  uint8_t logLevel;  // LogLevel
  uint32_t baud;     // serial baud rate
};

namespace config {
void begin();              // load from NVS (seed from secrets.h defaults if empty)
Config &get();             // mutable in-memory copy
void save();               // persist current in-memory copy to NVS
void setLogLevel(uint8_t lvl);
}  // namespace config
