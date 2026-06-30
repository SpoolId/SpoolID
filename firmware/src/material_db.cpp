#include "material_db.h"

#include <HTTPClient.h>
#include <LittleFS.h>

#include "logger.h"
#include "material_db_default.h"

namespace matdb {
namespace {
constexpr char PATH_GZ[] = "/db.json.gz";   // client-uploaded, pre-gzipped
constexpr char PATH_RAW[] = "/db.json";     // printer-pulled, uncompressed

bool exists(const char *p) { return LittleFS.exists(p); }

bool writeFile(const char *path, const uint8_t *data, size_t len) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  size_t n = f.write(data, len);
  f.close();
  return n == len;
}
}  // namespace

void begin() {
  if (!LittleFS.begin(/*formatOnFail=*/true)) {
    LOG_E("LittleFS mount failed");
  }
}

void serveTo(AsyncWebServerRequest *request) {
  if (exists(PATH_GZ)) {
    AsyncWebServerResponse *resp = request->beginResponse(LittleFS, PATH_GZ, "application/json");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
    return;
  }
  if (exists(PATH_RAW)) {
    request->send(LittleFS, PATH_RAW, "application/json");
    return;
  }
  // PROGMEM default (gzipped).
  AsyncWebServerResponse *resp = request->beginResponse_P(
      200, "application/json", MATERIAL_DB_DEFAULT, MATERIAL_DB_DEFAULT_LEN);
  resp->addHeader("Content-Encoding", "gzip");
  request->send(resp);
}

bool storeGzip(const uint8_t *data, size_t len) {
  LittleFS.remove(PATH_RAW);  // gzip upload supersedes any pulled raw copy
  bool ok = writeFile(PATH_GZ, data, len);
  LOG_I("stored uploaded DB gzip (%u B) ok=%d", (unsigned)len, ok);
  return ok;
}

bool storeRaw(const uint8_t *data, size_t len) {
  LittleFS.remove(PATH_GZ);
  bool ok = writeFile(PATH_RAW, data, len);
  LOG_I("stored raw DB (%u B) ok=%d", (unsigned)len, ok);
  return ok;
}

bool pullFromPrinter(const String &host, String &err) {
  String url = "http://" + host + "/downloads/defData/material_database.json";
  LOG_I("pulling DB from %s", url.c_str());
  HTTPClient http;
  if (!http.begin(url)) {
    err = "http begin failed";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    err = String("HTTP ") + code;
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  File f = LittleFS.open(PATH_RAW, "w");
  if (!f) {
    err = "fs open failed";
    http.end();
    return false;
  }
  uint8_t buf[512];
  int total = 0;
  while (http.connected()) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      f.write(buf, n);
      total += n;
    } else if (!stream->connected()) {
      break;
    } else {
      delay(1);
    }
  }
  f.close();
  http.end();
  LittleFS.remove(PATH_GZ);  // raw pull supersedes uploaded gzip
  LOG_I("pulled DB raw (%d B)", total);
  return total > 0;
}

}  // namespace matdb
