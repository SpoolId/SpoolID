#pragma once
#include <Arduino.h>

// Builds the 48-character Creality spoolData payload that gets encrypted into tag
// blocks 4/5/6 (16 chars each). Layout (48 chars total):
//   "AB124"(5) + vendorId"0276"(4) + "A2"(2) + filamentId(6) + color(7) +
//   filamentLen(4) + serialNum(6) + reserve"000000"(6) + "00000000"(8)
// filamentId = "1" + base.id (5-char Creality material code, e.g. 01001 -> 101001).
// color      = "0" + RRGGBB (uppercase hex, no '#').
namespace spool {

constexpr char VENDOR_CREALITY[] = "0276";

// Map a spool size label to the Creality length code.
//   1KG=0330  750G=0247  600G=0198  500G=0165  250G=0082
// Returns "" for an unknown label.
String weightCode(const String &sizeLabel);

// Build the 48-char payload. materialId is the 5-char base.id; rgb is 6 hex chars
// (no '#', case-insensitive); sizeLabel is one of the supported size labels. If
// serial6 is empty a random 6-digit serial is generated. Returns "" on bad input.
String build(const String &materialId, const String &rgb, const String &sizeLabel, const String &serial6 = "");

}  // namespace spool
