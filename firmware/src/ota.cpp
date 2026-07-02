#include "ota.h"

#include <Update.h>

#include "logger.h"

namespace ota {
namespace {
bool g_active = false;
uint32_t g_rebootAt = 0;  // millis() deadline; 0 = not scheduled
}  // namespace

bool begin(size_t size, bool filesystem, const String &md5) {
  if (g_active) Update.abort();
  int command = filesystem ? U_SPIFFS : U_FLASH;
  if (!Update.begin(size, command)) {
    LOG_E("OTA begin failed: %s", Update.errorString());
    return false;
  }
  if (md5.length() == 32) Update.setMD5(md5.c_str());
  g_active = true;
  LOG_I("OTA begin: %u bytes -> %s", (unsigned)size, filesystem ? "fs" : "app");
  return true;
}

bool write(const uint8_t *data, size_t len) {
  if (!g_active) return false;
  if (Update.write(const_cast<uint8_t *>(data), len) != len) {
    LOG_E("OTA write failed: %s", Update.errorString());
    Update.abort();
    g_active = false;
    return false;
  }
  return true;
}

bool end(String &err) {
  if (!g_active) {
    err = "no active OTA";
    return false;
  }
  g_active = false;
  if (!Update.end(true)) {
    err = Update.errorString();
    LOG_E("OTA end failed: %s", err.c_str());
    return false;
  }
  LOG_I("OTA end: success");
  return true;
}

void abort() {
  if (g_active) {
    Update.abort();
    g_active = false;
  }
}

bool active() { return g_active; }

void scheduleReboot(uint32_t delayMs) {
  g_rebootAt = millis() + delayMs;
  if (g_rebootAt == 0) g_rebootAt = 1;  // 0 means "unscheduled"
}

void loop() {
  if (g_rebootAt && millis() >= g_rebootAt) {
    LOG_I("OTA reboot");
    delay(50);
    ESP.restart();
  }
}

}  // namespace ota
