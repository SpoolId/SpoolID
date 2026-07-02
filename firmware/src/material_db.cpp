#include "material_db.h"

#include <LittleFS.h>

#include "logger.h"
#include "material_db_default.h"

namespace matdb {
namespace {
constexpr char PATH_GZ[] = "/db.json.gz";  // client-uploaded, pre-gzipped
File g_upload;                             // open file during a chunked upload
}  // namespace

void begin() {
  if (!LittleFS.begin(/*formatOnFail=*/true)) {
    LOG_E("LittleFS mount failed");
  }
}

void serveTo(AsyncWebServerRequest *request) {
  if (LittleFS.exists(PATH_GZ)) {
    AsyncWebServerResponse *resp = request->beginResponse(LittleFS, PATH_GZ, "application/json");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
    return;
  }
  // PROGMEM default (gzipped).
  AsyncWebServerResponse *resp = request->beginResponse_P(
      200, "application/json", MATERIAL_DB_DEFAULT, MATERIAL_DB_DEFAULT_LEN);
  resp->addHeader("Content-Encoding", "gzip");
  request->send(resp);
}

bool uploadBegin() {
  g_upload = LittleFS.open(PATH_GZ, "w");
  return static_cast<bool>(g_upload);
}

bool uploadChunk(const uint8_t *data, size_t len) {
  if (!g_upload) return false;
  return g_upload.write(data, len) == len;
}

bool uploadEnd() {
  if (!g_upload) return false;
  g_upload.close();
  LOG_I("stored uploaded DB gzip");
  return true;
}

}  // namespace matdb
