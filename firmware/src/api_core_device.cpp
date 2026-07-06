#include <Arduino.h>

#include "api_core.h"
#include "config.h"
#include "logger.h"
#include "rfid_writer.h"
#include "spool_data.h"
#include "wifi_net.h"

// Device-side gatherers: collect data from the hardware/config modules and
// delegate to the pure shape layer. Excluded from the native test env.
namespace apicore {

bool fillRead(JsonDocument &doc, const char *&errCode) {
  String data, uid;
  bool enc;
  if (!rfid::readTag(data, uid, enc)) {
    errCode = "no_tag";
    return false;
  }
  spool::Parsed p = spool::parse(data);
  shape::read(doc, {uid.c_str(), enc, p.materialId.c_str(), p.color.c_str(),
                    p.weight.c_str(), p.serial.c_str()});
  return true;
}

bool fillDump(JsonDocument &doc, const char *&errCode) {
  String hex, uid;
  bool enc;
  if (!rfid::dumpTag(hex, uid, enc)) {
    errCode = "no_tag";
    return false;
  }
  shape::dump(doc, {uid.c_str(), enc, hex.c_str()});
  return true;
}

void fillStatus(JsonDocument &doc) {
  const rfid::WriteResult &r = rfid::last();
  String ip = net::ip();
  shape::status(doc, {rfid::hasPending(),
                      net::isAp() ? "ap" : "sta",
                      ip.c_str(),
                      {r.done, r.ok, r.encrypted, r.uid.c_str(), r.error.c_str()}});
}

void fillConfigGet(JsonDocument &doc) {
  Config &c = config::get();
  // passwords intentionally omitted
  shape::configGet(doc, {c.wifiSsid.c_str(), c.apSsid.c_str(), c.hostname.c_str(),
                         c.logLevel, c.baud});
}

void applyConfigSet(JsonDocument &in) {
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
}

}  // namespace apicore
