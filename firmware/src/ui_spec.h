#pragma once
#include <ArduinoJson.h>

// Firmware-owned UI constants (color swatches, spool-size labels, baud rates, log
// levels). The firmware is the single source of truth; web and desktop clients
// fetch these at runtime (HTTP /api/spec, serial "getspec") instead of hardcoding
// their own copies. Behavior-critical data (AES keys, spoolData format, weight
// length codes) stays firmware-only and is never exposed.
namespace uispec {
// Populate `doc` with colorSwatches[], weightLabels[], baudRates[], logLevels[].
void fill(JsonDocument &doc);
}  // namespace uispec
