#include "web_server.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "api_core.h"
#include "logger.h"
#include "material_db.h"
#include "ota.h"
#include "rfid_writer.h"
#include "spool_data.h"
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

void sendJson(AsyncWebServerRequest *r, int status, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  r->send(status, "application/json", out);
}

void sendErr(AsyncWebServerRequest *r, int status, const char *code, const char *msg) {
  JsonDocument d;
  apicore::shape::error(d, code, msg);
  sendJson(r, status, d);
}

void sendOk(AsyncWebServerRequest *r, void (*fill)(JsonDocument &)) {
  JsonDocument d;
  fill(d);
  sendJson(r, 200, d);
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
  server.on("/index.js", HTTP_GET, [](AsyncWebServerRequest *r) { sendGz(r, "/index.js.gz", "application/javascript"); });
  server.on("/config.js", HTTP_GET, [](AsyncWebServerRequest *r) { sendGz(r, "/config.js.gz", "application/javascript"); });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *r) { sendGz(r, "/style.css.gz", "text/css"); });

  // ---- UI spec (firmware-owned constants for the clients) ----
  server.on("/api/spec", HTTP_GET,
            [](AsyncWebServerRequest *r) { sendOk(r, apicore::shape::spec); });

  // ---- material DB ----
  server.on("/api/db", HTTP_GET, [](AsyncWebServerRequest *r) { matdb::serveTo(r); });

  // Upload pre-gzipped DB: stream body chunks directly to LittleFS.
  server.on(
      "/api/db", HTTP_POST,
      [](AsyncWebServerRequest *r) { sendOk(r, apicore::shape::ack); },
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

  // ---- write ----
  server.on(
      "/api/write", HTTP_POST,
      [](AsyncWebServerRequest *r) {},
      nullptr,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          sendErr(r, 400, "bad_json", "bad json");
          return;
        }
        String payload = spool::build(doc["materialId"] | "", doc["color"] | "",
                                      doc["weight"] | "");
        if (payload.isEmpty()) {
          sendErr(r, 400, "invalid_params", "invalid spool params");
          return;
        }
        rfid::stage(payload);
        sendOk(r, apicore::shape::writeStaged);
      });

  // ---- read (decrypt a tapped tag: for inspection / cloning) ----
  server.on("/api/read", HTTP_GET, [](AsyncWebServerRequest *r) {
    JsonDocument doc;
    const char *code;
    if (!apicore::fillRead(doc, code)) {
      sendErr(r, 404, code, "no tag / auth failed");
      return;
    }
    sendJson(r, 200, doc);
  });

  // ---- dump (raw tag blocks as hex, uninterpreted) ----
  server.on("/api/dump", HTTP_GET, [](AsyncWebServerRequest *r) {
    JsonDocument doc;
    const char *code;
    if (!apicore::fillDump(doc, code)) {
      sendErr(r, 404, code, "no tag / auth failed");
      return;
    }
    sendJson(r, 200, doc);
  });

  // ---- buzzer test ----
  server.on("/api/beep", HTTP_GET, [](AsyncWebServerRequest *r) {
    rfid::testBeep();
    sendOk(r, apicore::shape::ack);
  });

  // ---- status ----
  server.on("/api/status", HTTP_GET,
            [](AsyncWebServerRequest *r) { sendOk(r, apicore::fillStatus); });

  // ---- config ----
  server.on("/api/config", HTTP_GET,
            [](AsyncWebServerRequest *r) { sendOk(r, apicore::fillConfigGet); });

  server.on(
      "/api/config", HTTP_POST,
      [](AsyncWebServerRequest *r) {},
      nullptr,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          sendErr(r, 400, "bad_json", "bad json");
          return;
        }
        apicore::applyConfigSet(doc);
        sendOk(r, apicore::shape::ackReboot);
      });

  // ---- OTA firmware/filesystem update ----
  // POST the raw image; `?target=app|fs` picks the app slot vs the LittleFS
  // partition, `?md5=<32hex>` is verified on completion. Body is streamed
  // straight into Update (device self-flashes). Reboots after the reply flushes.
  // One OTA at a time — the outcome is stashed here between the body + request
  // callbacks (the request handler runs once the body is fully received).
  static bool s_otaOk;
  static String s_otaErr;
  server.on(
      "/api/ota", HTTP_POST,
      [](AsyncWebServerRequest *r) {
        if (s_otaOk) {
          sendOk(r, apicore::shape::ack);
          ota::scheduleReboot();
        } else {
          sendErr(r, 500, "ota_failed", s_otaErr.c_str());
        }
      },
      nullptr,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          bool fs = r->hasParam("target") && r->getParam("target")->value() == "fs";
          String md5 = r->hasParam("md5") ? r->getParam("md5")->value() : String();
          s_otaOk = ota::begin(total, fs, md5);
          s_otaErr = s_otaOk ? "" : "begin failed";
          if (!s_otaOk) return;
        }
        if (s_otaOk && !ota::write(data, len)) {
          s_otaOk = false;
          s_otaErr = "write failed";
          return;
        }
        if (index + len == total && s_otaOk && !ota::end(s_otaErr)) {
          s_otaOk = false;
        }
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
