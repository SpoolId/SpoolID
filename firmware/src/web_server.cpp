#include "web_server.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "config.h"
#include "logger.h"
#include "material_db.h"
#include "rfid_writer.h"
#include "spool_data.h"
#include "ui_spec.h"
#include "wifi_net.h"

namespace web {
namespace {
AsyncWebServer server(80);

// Serve a gzipped static asset from LittleFS.
void sendGz(AsyncWebServerRequest *req, const char *path, const char *type) {
  if (!LittleFS.exists(path)) {
    req->send(404, "text/plain", "missing asset (run build_web.py + uploadfs)");
    return;
  }
  AsyncWebServerResponse *resp = req->beginResponse(LittleFS, path, type);
  resp->addHeader("Content-Encoding", "gzip");
  req->send(resp);
}

String statusJson() {
  const rfid::WriteResult &r = rfid::last();
  JsonDocument doc;
  doc["pending"] = rfid::hasPending();
  doc["mode"] = net::isAp() ? "ap" : "sta";
  doc["ip"] = net::ip();
  JsonObject last = doc["last"].to<JsonObject>();
  last["done"] = r.done;
  last["ok"] = r.ok;
  last["encrypted"] = r.encrypted;
  last["uid"] = r.uid;
  last["error"] = r.error;
  String out;
  serializeJson(doc, out);
  return out;
}

// Accumulate a binary request body across chunks into a buffer (small payloads:
// config JSON, pull request). For the potentially-large DB upload we stream
// straight to LittleFS instead (see /api/db handler).
struct BodyBuf {
  String data;
};

void registerRoutes() {
  // ---- static UI ----
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { sendGz(r, "/index.html.gz", "text/html"); });
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *r) { sendGz(r, "/config.html.gz", "text/html"); });
  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *r) { sendGz(r, "/app.js.gz", "application/javascript"); });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *r) { sendGz(r, "/style.css.gz", "text/css"); });

  // ---- UI spec (firmware-owned constants for the clients) ----
  server.on("/api/spec", HTTP_GET, [](AsyncWebServerRequest *r) {
    JsonDocument doc;
    uispec::fill(doc);
    String out;
    serializeJson(doc, out);
    r->send(200, "application/json", out);
  });

  // ---- material DB ----
  server.on("/api/db", HTTP_GET, [](AsyncWebServerRequest *r) { matdb::serveTo(r); });

  // Upload pre-gzipped DB: stream body chunks directly to LittleFS.
  server.on(
      "/api/db", HTTP_POST,
      [](AsyncWebServerRequest *r) { r->send(200, "application/json", "{\"ok\":true}"); },
      nullptr,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        static File f;
        if (index == 0) f = LittleFS.open("/db.json.gz", "w");
        if (f) f.write(data, len);
        if (index + len == total && f) {
          f.close();
          LittleFS.remove("/db.json");  // gzip upload supersedes pulled raw
          LOG_I("DB upload complete (%u B)", (unsigned)total);
        }
      });

  // Pull DB from a printer host: {"host":"192.168.1.50"}.
  server.on(
      "/api/db/pull", HTTP_POST,
      [](AsyncWebServerRequest *r) {},  // response sent from body handler
      nullptr,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          r->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        String host = doc["host"] | "";
        if (host.isEmpty()) {
          r->send(400, "application/json", "{\"ok\":false,\"error\":\"host required\"}");
          return;
        }
        String err;
        bool ok = matdb::pullFromPrinter(host, err);
        JsonDocument res;
        res["ok"] = ok;
        if (!ok) res["error"] = err;
        String out;
        serializeJson(res, out);
        r->send(ok ? 200 : 502, "application/json", out);
      });

  // ---- write ----
  server.on(
      "/api/write", HTTP_POST,
      [](AsyncWebServerRequest *r) {},
      nullptr,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          r->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        String id = doc["materialId"] | "";
        String color = doc["color"] | "";
        String weight = doc["weight"] | "";
        String payload = spool::build(id, color, weight);
        if (payload.isEmpty()) {
          r->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid spool params\"}");
          return;
        }
        rfid::stage(payload);
        r->send(200, "application/json", "{\"ok\":true,\"staged\":true}");
      });

  // ---- status ----
  server.on("/api/status", HTTP_GET,
            [](AsyncWebServerRequest *r) { r->send(200, "application/json", statusJson()); });

  // ---- config ----
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *r) {
    Config &c = config::get();
    JsonDocument doc;
    doc["wifiSsid"] = c.wifiSsid;
    doc["apSsid"] = c.apSsid;
    doc["hostname"] = c.hostname;
    doc["logLevel"] = c.logLevel;
    doc["baud"] = c.baud;
    // passwords intentionally omitted
    String out;
    serializeJson(doc, out);
    r->send(200, "application/json", out);
  });

  server.on(
      "/api/config", HTTP_POST,
      [](AsyncWebServerRequest *r) {},
      nullptr,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          r->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        Config &c = config::get();
        if (doc["wifiSsid"].is<const char *>()) c.wifiSsid = doc["wifiSsid"].as<String>();
        if (doc["wifiPass"].is<const char *>()) c.wifiPass = doc["wifiPass"].as<String>();
        if (doc["apSsid"].is<const char *>()) c.apSsid = doc["apSsid"].as<String>();
        if (doc["apPass"].is<const char *>()) c.apPass = doc["apPass"].as<String>();
        if (doc["hostname"].is<const char *>()) c.hostname = doc["hostname"].as<String>();
        if (doc["logLevel"].is<uint8_t>()) c.logLevel = doc["logLevel"];
        if (doc["baud"].is<uint32_t>()) c.baud = doc["baud"];
        config::save();
        logger::setLevel(static_cast<LogLevel>(c.logLevel));
        r->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
      });

  // Captive portal: send unknown hosts to the config page when in AP mode.
  server.onNotFound([](AsyncWebServerRequest *r) {
    if (net::isAp()) {
      r->redirect("/config");
    } else {
      r->send(404, "text/plain", "not found");
    }
  });
}
}  // namespace

void begin() {
  registerRoutes();
  server.begin();
  LOG_I("web server on http://%s", net::ip().c_str());
}

}  // namespace web
