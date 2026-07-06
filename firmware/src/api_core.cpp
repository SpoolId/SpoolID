#include "api_core.h"

#include "ui_spec.h"
#include "version.h"

// Pure shape layer only — keep this file free of Arduino/rfid/WiFi includes so
// it compiles in the native test env (platformio.ini [env:native]).
namespace apicore {
namespace shape {

void spec(JsonDocument &doc) {
  doc["ok"] = true;
  doc["protocol"] = PROTOCOL_VERSION;
  uispec::fill(doc);  // version + colorSwatches + weightLabels + baudRates + logLevels
}

void read(JsonDocument &doc, const ReadData &d) {
  doc["ok"] = true;
  doc["uid"] = d.uid;
  doc["encrypted"] = d.encrypted;
  doc["materialId"] = d.materialId;
  doc["color"] = d.color;
  doc["weight"] = d.weight;
  doc["serial"] = d.serial;
}

void dump(JsonDocument &doc, const DumpData &d) {
  doc["ok"] = true;
  doc["uid"] = d.uid;
  doc["encrypted"] = d.encrypted;
  doc["data"] = d.data;
}

void status(JsonDocument &doc, const StatusData &d) {
  doc["ok"] = true;
  doc["pending"] = d.pending;
  doc["mode"] = d.mode;
  doc["ip"] = d.ip;
  JsonObject last = doc["last"].to<JsonObject>();
  last["done"] = d.last.done;
  last["ok"] = d.last.ok;
  last["encrypted"] = d.last.encrypted;
  last["uid"] = d.last.uid;
  last["error"] = d.last.error;
}

void configGet(JsonDocument &doc, const ConfigData &d) {
  doc["ok"] = true;
  doc["wifiSsid"] = d.wifiSsid;
  doc["apSsid"] = d.apSsid;
  doc["hostname"] = d.hostname;
  doc["logLevel"] = d.logLevel;
  doc["baud"] = d.baud;
}

void writeStaged(JsonDocument &doc) {
  doc["ok"] = true;
  doc["staged"] = true;
}

void ack(JsonDocument &doc) { doc["ok"] = true; }

void ackReboot(JsonDocument &doc) {
  doc["ok"] = true;
  doc["reboot"] = true;
}

void written(JsonDocument &doc, uint32_t bytes) {
  doc["ok"] = true;
  doc["written"] = bytes;
}

void error(JsonDocument &doc, const char *code, const char *msg) {
  doc["ok"] = false;
  doc["error"] = msg;
  doc["code"] = code;
}

}  // namespace shape
}  // namespace apicore
