#pragma once
#include <Arduino.h>

// Runtime-adjustable serial logging. Level + baud are persisted in NVS (config.h)
// and applied at boot; the web/serial config pages can change the level live.
enum LogLevel : uint8_t {
  LOG_NONE = 0,
  LOG_ERROR = 1,
  LOG_WARN = 2,
  LOG_INFO = 3,
  LOG_DEBUG = 4,
};

namespace logger {
void begin(uint32_t baud, LogLevel level);
void setLevel(LogLevel level);
LogLevel level();
void printf(LogLevel lvl, const char *fmt, ...);
}  // namespace logger

#define LOG_E(...) logger::printf(LOG_ERROR, __VA_ARGS__)
#define LOG_W(...) logger::printf(LOG_WARN, __VA_ARGS__)
#define LOG_I(...) logger::printf(LOG_INFO, __VA_ARGS__)
#define LOG_D(...) logger::printf(LOG_DEBUG, __VA_ARGS__)
