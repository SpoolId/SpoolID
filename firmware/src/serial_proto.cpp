#include "serial_proto.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

#include "api_core.h"
#include "material_db.h"
#include "ota.h"
#include "rfid_writer.h"
#include "spool_data.h"

// Max raw bytes per OTA chunk; the base64 of this fits a serial line (see poll).
static constexpr size_t OTA_CHUNK = 4096;

namespace serialproto {
namespace {

void reply(const JsonDocument &doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

void replyErr(const char *code, const char *msg) {
  JsonDocument d;
  apicore::shape::error(d, code, msg);
  reply(d);
}

void replyOk(void (*fill)(JsonDocument &)) {
  JsonDocument d;
  fill(d);
  reply(d);
}

void cmdWrite(JsonDocument &in) {
  String payload = spool::build(in["materialId"] | "", in["color"] | "", in["weight"] | "");
  if (payload.isEmpty()) return replyErr("invalid_params", "invalid spool params");
  rfid::stage(payload);
  replyOk(apicore::shape::writeStaged);
}

void cmdRead() {
  JsonDocument d;
  const char *code;
  if (!apicore::fillRead(d, code)) return replyErr(code, "no tag / auth failed");
  reply(d);
}

void cmdDump() {
  JsonDocument d;
  const char *code;
  if (!apicore::fillDump(d, code)) return replyErr(code, "no tag / auth failed");
  reply(d);
}

// ---- material DB upload over serial (chunked base64, pre-gzipped) ----
// dbbegin {size} -> dbdata {b:"<base64>"} * N -> dbend. The client gzips the
// JSON (the ESP32 has no runtime compressor) and streams it to LittleFS.
void cmdDbBegin(JsonDocument &in) {
  size_t size = in["size"] | 0;
  if (!size) return replyErr("size_required", "size required");
  if (!matdb::uploadBegin()) return replyErr("db_failed", "db open failed");
  replyOk(apicore::shape::ack);
}

void cmdDbData(JsonDocument &in) {
  const char *b = in["b"] | "";
  static uint8_t buf[OTA_CHUNK];
  size_t outLen = 0;
  if (mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                            reinterpret_cast<const uint8_t *>(b), strlen(b)) != 0) {
    return replyErr("bad_chunk", "bad base64 / chunk too big");
  }
  if (!matdb::uploadChunk(buf, outLen)) return replyErr("db_failed", "db write failed");
  JsonDocument d;
  apicore::shape::written(d, static_cast<uint32_t>(outLen));
  reply(d);
}

void cmdDbEnd() {
  if (!matdb::uploadEnd()) return replyErr("db_failed", "db finalize failed");
  replyOk(apicore::shape::ack);
}

// ---- OTA over serial (chunked base64) ----
// otabegin {size,target:"app"|"fs",md5?} -> otadata {b:"<base64>"} * N -> otaend.
// Device self-flashes via Update; otaend reboots after the reply flushes.
void cmdOtaBegin(JsonDocument &in) {
  size_t size = in["size"] | 0;
  bool fs = String(in["target"] | "app") == "fs";
  String md5 = in["md5"] | "";
  if (!size) return replyErr("size_required", "size required");
  if (!ota::begin(size, fs, md5)) return replyErr("ota_failed", "ota begin failed");
  replyOk(apicore::shape::ack);
}

void cmdOtaData(JsonDocument &in) {
  const char *b = in["b"] | "";
  static uint8_t buf[OTA_CHUNK];
  size_t outLen = 0;
  int rc = mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                                 reinterpret_cast<const uint8_t *>(b), strlen(b));
  if (rc != 0) {
    ota::abort();
    return replyErr("bad_chunk", "bad base64 / chunk too big");
  }
  if (!ota::write(buf, outLen)) return replyErr("ota_failed", "ota write failed");
  JsonDocument d;
  apicore::shape::written(d, static_cast<uint32_t>(outLen));
  reply(d);
}

void cmdOtaEnd() {
  String err;
  if (!ota::end(err)) return replyErr("ota_failed", err.c_str());
  replyOk(apicore::shape::ackReboot);
  ota::scheduleReboot();
}

void dispatch(const String &line) {
  JsonDocument in;
  if (deserializeJson(in, line)) return replyErr("bad_json", "bad json");
  String cmd = in["cmd"] | "";
  if (cmd == "write") cmdWrite(in);
  else if (cmd == "status") replyOk(apicore::fillStatus);
  else if (cmd == "dump") cmdDump();
  else if (cmd == "read") cmdRead();
  else if (cmd == "beep") { rfid::testBeep(); replyOk(apicore::shape::ack); }
  else if (cmd == "getconfig") replyOk(apicore::fillConfigGet);
  else if (cmd == "setconfig") { apicore::applyConfigSet(in); replyOk(apicore::shape::ackReboot); }
  else if (cmd == "getspec") replyOk(apicore::shape::spec);
  else if (cmd == "dbbegin") cmdDbBegin(in);
  else if (cmd == "dbdata") cmdDbData(in);
  else if (cmd == "dbend") cmdDbEnd();
  else if (cmd == "otabegin") cmdOtaBegin(in);
  else if (cmd == "otadata") cmdOtaData(in);
  else if (cmd == "otaend") cmdOtaEnd();
  else if (cmd == "otaabort") { ota::abort(); replyOk(apicore::shape::ack); }
  else replyErr("unknown_cmd", "unknown cmd");
}

}  // namespace

void poll() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {  // accept CR, LF, or CRLF (empty lines skipped)
      line.trim();
      if (line.length()) dispatch(line);
      line = "";
    } else if (line.length() < 6144) {  // fits a base64 OTA chunk + JSON wrapper
      line += ch;
    }
  }
}

}  // namespace serialproto
