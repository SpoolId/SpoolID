#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Serves the Creality material database to the UI and accepts replacements.
// Priority when serving: client-uploaded gzip (/db.json.gz) > printer-pulled raw
// (/db.json) > the gzipped default baked into PROGMEM. The ESP32 has no runtime
// gzip compressor, so uploads arrive pre-gzipped (CompressionStream in the browser)
// and printer-pulled JSON is stored raw and served without Content-Encoding.
namespace matdb {

void begin();  // mounts LittleFS

// Write the active response to a request, choosing gzip/raw + headers correctly.
void serveTo(AsyncWebServerRequest *request);

// Store a client-uploaded, already-gzipped DB to LittleFS. Returns false on FS error.
bool storeGzip(const uint8_t *data, size_t len);

// Store raw (uncompressed) JSON pulled from a printer.
bool storeRaw(const uint8_t *data, size_t len);

// Fetch material_database.json directly from a printer host and store it raw.
// host is "ip" or "ip:port"; returns false on HTTP error. err gets a message.
bool pullFromPrinter(const String &host, String &err);

}  // namespace matdb
