#pragma once
#include <ArduinoJson.h>

#include <cstdint>

// Single owner of every wire reply shape (spec/v2/PROTOCOL.md). Two layers:
//
//   shape::*  — pure fillers over plain structs. No Arduino / rfid / WiFi
//               headers, so they compile on the host: the native contract test
//               (test/test_contract) runs these exact functions and validates
//               their output against spec/v2/schemas in CI.
//   fill*     — device-side gatherers (api_core_device.cpp): collect data from
//               rfid:: / config:: / net::, fill the struct, delegate to shape::*.
//
// Transport handlers (web_server.cpp, serial_proto.cpp) call only this API and
// contain no JSON keys. Any change here must update spec/v2/schemas + fixtures
// in the same PR.
namespace apicore {

struct ReadData {
  const char *uid;
  bool encrypted;
  const char *materialId, *color, *weight, *serial;
};

struct DumpData {
  const char *uid;
  bool encrypted;
  const char *data;  // raw tag blocks as hex, uninterpreted
};

struct LastWrite {
  bool done, ok, encrypted;
  const char *uid, *error;
};

struct StatusData {
  bool pending;
  const char *mode, *ip;  // mode: "ap" | "sta"
  LastWrite last;
};

struct ConfigData {
  const char *wifiSsid, *apSsid, *hostname;  // passwords never leave the device
  uint8_t logLevel;
  uint32_t baud;
};

namespace shape {
void spec(JsonDocument &doc);  // ok + protocol + uispec lists/version
void read(JsonDocument &doc, const ReadData &d);
void dump(JsonDocument &doc, const DumpData &d);
void status(JsonDocument &doc, const StatusData &d);
void configGet(JsonDocument &doc, const ConfigData &d);
void writeStaged(JsonDocument &doc);              // {ok, staged}
void ack(JsonDocument &doc);                      // {ok}
void ackReboot(JsonDocument &doc);                // {ok, reboot}
void written(JsonDocument &doc, uint32_t bytes);  // {ok, written}
// code is one of the stable ErrorCode values from spec/v2/schemas/common.
void error(JsonDocument &doc, const char *code, const char *msg);
}  // namespace shape

// Device layer (api_core_device.cpp — not built in the native test env).
// The bool gatherers return false with errCode set (caller wraps via shape::error).
bool fillRead(JsonDocument &doc, const char *&errCode);
bool fillDump(JsonDocument &doc, const char *&errCode);
void fillStatus(JsonDocument &doc);
void fillConfigGet(JsonDocument &doc);
void applyConfigSet(JsonDocument &in);  // set fields, persist, apply log level

}  // namespace apicore
