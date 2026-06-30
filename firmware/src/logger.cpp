#include "logger.h"

#include <stdarg.h>

namespace {
LogLevel g_level = LOG_INFO;
const char *tag(LogLevel l) {
  switch (l) {
    case LOG_ERROR: return "E";
    case LOG_WARN:  return "W";
    case LOG_INFO:  return "I";
    case LOG_DEBUG: return "D";
    default:        return "?";
  }
}
}  // namespace

namespace logger {

void begin(uint32_t baud, LogLevel level) {
  Serial.begin(baud);
  // USB-Serial-JTAG: wait briefly for the host to open the port so the boot logs
  // aren't dropped. Bounded so a headless boot (no host) still proceeds.
  uint32_t start = millis();
  while (!Serial && millis() - start < 2000) delay(10);
  delay(100);
  g_level = level;
}

void setLevel(LogLevel level) { g_level = level; }
LogLevel level() { return g_level; }

void printf(LogLevel lvl, const char *fmt, ...) {
  if (lvl > g_level || g_level == LOG_NONE) return;
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("[%lu][%s] %s\n", millis(), tag(lvl), buf);
}

}  // namespace logger
