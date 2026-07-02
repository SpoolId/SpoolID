#pragma once

#include <Arduino.h>

// Over-the-air update core. Thin wrapper over the Arduino Update library: the
// device always does the actual flashing (with MD5 verify + A/B rollback);
// clients only stream image bytes in. Exposed through both transports —
// web `/api/ota` (WiFi POST) and the serial `otabegin/otadata/otaend` commands.
namespace ota {

// Start an update session. `filesystem` selects the LittleFS image (U_SPIFFS)
// vs the app (U_FLASH). `md5` (32 hex chars) is optional; if given, Update
// verifies it on end(). Returns false if a session can't be started.
bool begin(size_t size, bool filesystem, const String &md5);

// Write the next chunk. Returns false on any error (session is left aborted).
bool write(const uint8_t *data, size_t len);

// Finalize: verify + set the new boot slot. On failure fills `err`. The caller
// is responsible for rebooting (see scheduleReboot).
bool end(String &err);

// Cancel an in-progress session.
void abort();

// True while a session is active.
bool active();

// Reboot after `delayMs` (lets an HTTP/serial reply flush first). Handled in loop().
void scheduleReboot(uint32_t delayMs = 500);

// Call from the main loop: performs a scheduled reboot when due.
void loop();

}  // namespace ota
