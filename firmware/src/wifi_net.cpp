#include "wifi_net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "logger.h"

namespace net {
namespace {
DNSServer g_dns;
Mode g_mode = Mode::Connecting;
constexpr uint32_t STA_TIMEOUT_MS = 15000;
constexpr byte DNS_PORT = 53;

void startAp() {
  Config &c = config::get();
  WiFi.mode(WIFI_AP);
  bool ok = c.apPass.length() >= 8 ? WiFi.softAP(c.apSsid.c_str(), c.apPass.c_str())
                                   : WiFi.softAP(c.apSsid.c_str());
  IPAddress ip = WiFi.softAPIP();
  g_dns.start(DNS_PORT, "*", ip);  // captive portal: resolve everything to us
  g_mode = Mode::AccessPoint;
  LOG_I("AP '%s' up=%d ip=%s", c.apSsid.c_str(), ok, ip.toString().c_str());
}

// Advertise <hostname>.local once we have an interface up.
void startMdns() {
  Config &c = config::get();
  if (c.hostname.isEmpty()) return;
  if (MDNS.begin(c.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    LOG_I("mDNS: http://%s.local", c.hostname.c_str());
  } else {
    LOG_W("mDNS start failed");
  }
}
}  // namespace

void begin() {
  Config &c = config::get();
  if (c.wifiSsid.isEmpty()) {
    LOG_I("no WiFi SSID configured -> AP mode");
    startAp();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(c.wifiSsid.c_str(), c.wifiPass.c_str());
  LOG_I("connecting to '%s'", c.wifiSsid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_TIMEOUT_MS) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    g_mode = Mode::Station;
    LOG_I("STA connected ip=%s", WiFi.localIP().toString().c_str());
  } else {
    LOG_W("STA connect failed -> AP fallback");
    startAp();
  }
  startMdns();
}

void loop() {
  if (g_mode == Mode::AccessPoint) g_dns.processNextRequest();
}

Mode mode() { return g_mode; }
bool isAp() { return g_mode == Mode::AccessPoint; }

String ip() {
  return g_mode == Mode::AccessPoint ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

}  // namespace net
