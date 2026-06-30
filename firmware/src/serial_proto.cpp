#include "serial_proto.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "logger.h"
#include "material_db.h"
#include "rfid_writer.h"
#include "spool_data.h"
#include "ui_spec.h"
#include "wifi_net.h"

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

void cmdDbPull(JsonDocument &in) {
  String host = in["host"] | "";
  if (host.isEmpty()) return replyErr("host required");
  String err;
  bool ok = matdb::pullFromPrinter(host, err);
  JsonDocument d;
  d["ok"] = ok;
  if (!ok) d["error"] = err;
  reply(d);
}

void dispatch(const String &line) {
  JsonDocument in;
  if (deserializeJson(in, line)) return replyErr("bad json");
  String cmd = in["cmd"] | "";
  if (cmd == "write") cmdWrite(in);
  else if (cmd == "status") cmdStatus();
  else if (cmd == "dump") cmdDump();
  else if (cmd == "getconfig") cmdGetConfig();
  else if (cmd == "setconfig") cmdSetConfig(in);
  else if (cmd == "getspec") cmdGetSpec();
  else if (cmd == "dbpull") cmdDbPull(in);
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
    } else if (line.length() < 256) {
      line += ch;
    }
  }
}

}  // namespace serialproto
