#include "ui_spec.h"

namespace uispec {
namespace {
const char *COLOR_SWATCHES[] = {
    "1200F6", "3894E1", "FEFF01", "F8D531", "F38E24", "52D048", "00FEBE",
    "B700F3", "EE301A", "FA5959", "FFFFFF", "D8D8D8", "4C4C4C", "782543",
    "000000",
};
const char *WEIGHT_LABELS[] = {"1KG", "750G", "600G", "500G", "250G"};
const uint32_t BAUD_RATES[] = {9600, 57600, 115200, 230400, 460800, 921600};
const char *LOG_LEVELS[] = {"0 none", "1 error", "2 warn", "3 info", "4 debug"};
}  // namespace

void fill(JsonDocument &doc) {
  JsonArray swatches = doc["colorSwatches"].to<JsonArray>();
  for (const char *s : COLOR_SWATCHES) swatches.add(s);
  JsonArray weights = doc["weightLabels"].to<JsonArray>();
  for (const char *w : WEIGHT_LABELS) weights.add(w);
  JsonArray bauds = doc["baudRates"].to<JsonArray>();
  for (uint32_t b : BAUD_RATES) bauds.add(b);
  JsonArray levels = doc["logLevels"].to<JsonArray>();
  for (const char *l : LOG_LEVELS) levels.add(l);
}

}  // namespace uispec
