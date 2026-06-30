#pragma once
#include <Arduino.h>

// RC522 / MIFARE Classic writer for Creality spool tags. Non-blocking: stage() a
// 48-char payload, then call poll() each loop; when a compatible tag is tapped it
// runs the full encrypt+write+lock sequence and records the result in last().
namespace rfid {

// XIAO ESP32-C3 <-> RC522 pin map (see project spec).
constexpr int PIN_SS = 6;    // GPIO6 / D4
constexpr int PIN_RST = 5;   // GPIO5 / D3
constexpr int PIN_SCK = 8;   // GPIO8 / D8
constexpr int PIN_MISO = 9;  // GPIO9 / D9
constexpr int PIN_MOSI = 10; // GPIO10 / D10

struct WriteResult {
  bool done = false;       // a tag was processed since the last stage()
  bool ok = false;         // write succeeded
  bool encrypted = false;  // tag was already locked to its derived key
  String uid;              // hex UID
  String error;            // human-readable failure reason
};

// buzzerPin < 0 disables beeps.
void begin(int buzzerPin = -1);

// Stage the 48-char spoolData to write on the next tag tap.
void stage(const String &spoolData48);
bool hasPending();
void clearPending();

// Run one non-blocking poll cycle. Performs the write if a tag is present and a
// payload is staged. Safe to call every loop.
void poll();

const WriteResult &last();

// Diagnostic: read blocks 4/5/6 from a tapped tag (auth auto: default key then
// derived ekey) and return the 48 raw bytes as hex. For verifying the crypto
// against a known-good Creality tag. Returns false if no tag / auth fails.
bool dumpTag(String &outHex, String &uidHex, bool &encrypted);

}  // namespace rfid
