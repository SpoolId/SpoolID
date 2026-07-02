#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Serves the Creality material database to the UI and accepts replacements.
// Priority when serving: client-uploaded gzip (/db.json.gz) > the gzipped default
// baked into PROGMEM. The ESP32 has no runtime gzip compressor, so uploads arrive
// pre-gzipped (CompressionStream in the browser, compress/gzip in the desktop app).
namespace matdb {

void begin();  // mounts LittleFS

// Write the active response to a request, choosing gzip/default + headers.
void serveTo(AsyncWebServerRequest *request);

// Chunked upload of an already-gzipped DB over serial: uploadBegin() opens the
// file, uploadChunk() appends decoded bytes, uploadEnd() closes it. The web
// transport streams straight to LittleFS in web_server.cpp instead.
bool uploadBegin();
bool uploadChunk(const uint8_t *data, size_t len);
bool uploadEnd();

}  // namespace matdb
