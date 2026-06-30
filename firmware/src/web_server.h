#pragma once

// HTTP UI + JSON API. Serves the gzipped web assets from LittleFS and exposes the
// same operations the serial protocol does: stage a write, read status, manage the
// material DB, and edit configuration.
namespace web {
void begin();
}
