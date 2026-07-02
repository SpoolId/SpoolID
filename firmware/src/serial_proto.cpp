#include "serial_proto.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

#include "config.h"
#include "logger.h"
#include "material_db.h"
#include "ota.h"
#include "rfid_writer.h"
#include "spool_data.h"
#include "ui_spec.h"
#include "wifi_net.h"

// Max raw bytes per OTA chunk; the base64 of this fits a serial line (see poll).
static constexpr size_t OTA_CHUNK = 4096;

namespace serialproto {
namespace {

void reply(const JsonDocument &doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

void replyErr(const char *msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  reply(d);
}

void cmdWrite(JsonDocument &in) {
  String payload = spool::build(in["materialId"] | "", in["color"] | "", in["weight"] | "");
  if (payload.isEmpty()) return replyErr("invalid spool params");
  rfid::stage(payload);
  JsonDocument d;
  d["ok"] = true;
  d["staged"] = true;
  reply(d);
}

void cmdRead() {
  String data, uid;
  bool enc;
  if (!rfid::readTag(data, uid, enc)) return replyErr("no tag / auth failed");
  spool::Parsed p = spool::parse(data);
  JsonDocument d;
  d["ok"] = true;
  d["uid"] = uid;
  d["encrypted"] = enc;
  d["materialId"] = p.materialId;
  d["color"] = p.color;
  d["weight"] = p.weight;
  reply(d);
}

void cmdStatus() {
  const rfid::WriteResult &r = rfid::last();
  JsonDocument d;
  d["ok"] = true;
  d["pending"] = rfid::hasPending();
  d["mode"] = net::isAp() ? "ap" : "sta";
  d["ip"] = net::ip();
  JsonObject last = d["last"].to<JsonObject>();
  last["done"] = r.done;
  last["ok"] = r.ok;
  last["encrypted"] = r.encrypted;
  last["uid"] = r.uid;
  last["error"] = r.error;
  reply(d);
}

void cmdDump() {
  String hex, uid;
  bool enc;
  if (!rfid::dumpTag(hex, uid, enc)) return replyErr("no tag / auth failed");
  JsonDocument d;
  d["ok"] = true;
  d["uid"] = uid;
  d["encrypted"] = enc;
  d["data"] = hex;
  reply(d);
}

void cmdGetConfig() {
  Config &c = config::get();
  JsonDocument d;
  d["ok"] = true;
  d["wifiSsid"] = c.wifiSsid;
  d["apSsid"] = c.apSsid;
  d["hostname"] = c.hostname;
  d["logLevel"] = c.logLevel;
  d["baud"] = c.baud;
  reply(d);
}

void cmdSetConfig(JsonDocument &in) {
  Config &c = config::get();
  if (in["wifiSsid"].is<const char *>()) c.wifiSsid = in["wifiSsid"].as<String>();
  if (in["wifiPass"].is<const char *>()) c.wifiPass = in["wifiPass"].as<String>();
  if (in["apSsid"].is<const char *>()) c.apSsid = in["apSsid"].as<String>();
  if (in["apPass"].is<const char *>()) c.apPass = in["apPass"].as<String>();
  if (in["hostname"].is<const char *>()) c.hostname = in["hostname"].as<String>();
  if (in["logLevel"].is<uint8_t>()) c.logLevel = in["logLevel"];
  if (in["baud"].is<uint32_t>()) c.baud = in["baud"];
  config::save();
  logger::setLevel(static_cast<LogLevel>(c.logLevel));
  JsonDocument d;
  d["ok"] = true;
  d["reboot"] = true;
  reply(d);
}

void cmdGetSpec() {
  JsonDocument d;
  d["ok"] = true;
  uispec::fill(d);
  reply(d);
}

// ---- material DB upload over serial (chunked base64, pre-gzipped) ----
// dbbegin {size} -> dbdata {b:"<base64>"} * N -> dbend. The client gzips the
// JSON (the ESP32 has no runtime compressor) and streams it to LittleFS.
void cmdDbBegin() {
  if (!matdb::uploadBegin()) return replyErr("db open failed");
  JsonDocument d;
  d["ok"] = true;
  reply(d);
}

void cmdDbData(JsonDocument &in) {
  const char *b = in["b"] | "";
  static uint8_t buf[OTA_CHUNK];
  size_t outLen = 0;
  if (mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                            reinterpret_cast<const uint8_t *>(b), strlen(b)) != 0) {
    return replyErr("bad base64 / chunk too big");
  }
  if (!matdb::uploadChunk(buf, outLen)) return replyErr("db write failed");
  JsonDocument d;
  d["ok"] = true;
  reply(d);
}

void cmdDbEnd() {
  if (!matdb::uploadEnd()) return replyErr("db finalize failed");
  JsonDocument d;
  d["ok"] = true;
  reply(d);
}

// ---- OTA over serial (chunked base64) ----
// otabegin {size,target:"app"|"fs",md5?} -> otadata {b:"<base64>"} * N -> otaend.
// Device self-flashes via Update; otaend reboots after the reply flushes.
void cmdOtaBegin(JsonDocument &in) {
  size_t size = in["size"] | 0;
  bool fs = String(in["target"] | "app") == "fs";
  String md5 = in["md5"] | "";
  if (!size) return replyErr("size required");
  if (!ota::begin(size, fs, md5)) return replyErr("ota begin failed");
  JsonDocument d;
  d["ok"] = true;
  reply(d);
}

void cmdOtaData(JsonDocument &in) {
  const char *b = in["b"] | "";
  static uint8_t buf[OTA_CHUNK];
  size_t outLen = 0;
  int rc = mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                                 reinterpret_cast<const uint8_t *>(b), strlen(b));
  if (rc != 0) {
    ota::abort();
    return replyErr("bad base64 / chunk too big");
  }
  if (!ota::write(buf, outLen)) return replyErr("ota write failed");
  JsonDocument d;
  d["ok"] = true;
  d["written"] = static_cast<uint32_t>(outLen);
  reply(d);
}

void cmdOtaEnd() {
  String err;
  if (!ota::end(err)) return replyErr(err.c_str());
  JsonDocument d;
  d["ok"] = true;
  d["reboot"] = true;
  reply(d);
  ota::scheduleReboot();
}

void dispatch(const String &line) {
  JsonDocument in;
  if (deserializeJson(in, line)) return replyErr("bad json");
  String cmd = in["cmd"] | "";
  if (cmd == "write") cmdWrite(in);
  else if (cmd == "status") cmdStatus();
  else if (cmd == "dump") cmdDump();
  else if (cmd == "read") cmdRead();
  else if (cmd == "beep") { rfid::testBeep(); JsonDocument d; d["ok"] = true; reply(d); }
  else if (cmd == "getconfig") cmdGetConfig();
  else if (cmd == "setconfig") cmdSetConfig(in);
  else if (cmd == "getspec") cmdGetSpec();
  else if (cmd == "dbbegin") cmdDbBegin();
  else if (cmd == "dbdata") cmdDbData(in);
  else if (cmd == "dbend") cmdDbEnd();
  else if (cmd == "otabegin") cmdOtaBegin(in);
  else if (cmd == "otadata") cmdOtaData(in);
  else if (cmd == "otaend") cmdOtaEnd();
  else if (cmd == "otaabort") { ota::abort(); JsonDocument d; d["ok"] = true; reply(d); }
  else replyErr("unknown cmd");
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
