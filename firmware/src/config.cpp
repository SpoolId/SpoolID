#include "config.h"

#include <Preferences.h>

#include "secrets.h"

namespace {
Preferences prefs;
Config g_cfg;
constexpr char NS[] = "spoolid";
}  // namespace

namespace config {

void begin() {
  prefs.begin(NS, /*readOnly=*/false);
  g_cfg.wifiSsid = prefs.getString("wifiSsid", DEFAULT_WIFI_SSID);
  g_cfg.wifiPass = prefs.getString("wifiPass", DEFAULT_WIFI_PASS);
  g_cfg.apSsid = prefs.getString("apSsid", DEFAULT_AP_SSID);
  g_cfg.apPass = prefs.getString("apPass", DEFAULT_AP_PASS);
  g_cfg.hostname = prefs.getString("hostname", DEFAULT_HOSTNAME);
  g_cfg.logLevel = prefs.getUChar("logLevel", DEFAULT_LOG_LEVEL);
  g_cfg.baud = prefs.getUInt("baud", DEFAULT_BAUD);
}

Config &get() { return g_cfg; }

void save() {
  prefs.putString("wifiSsid", g_cfg.wifiSsid);
  prefs.putString("wifiPass", g_cfg.wifiPass);
  prefs.putString("apSsid", g_cfg.apSsid);
  prefs.putString("apPass", g_cfg.apPass);
  prefs.putString("hostname", g_cfg.hostname);
  prefs.putUChar("logLevel", g_cfg.logLevel);
  prefs.putUInt("baud", g_cfg.baud);
}

void setLogLevel(uint8_t lvl) {
  g_cfg.logLevel = lvl;
  prefs.putUChar("logLevel", lvl);
  logger::setLevel(static_cast<LogLevel>(lvl));
}

}  // namespace config
