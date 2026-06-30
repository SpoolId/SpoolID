#pragma once

// Line-delimited JSON command protocol over USB CDC, mirroring the web API so the
// .NET desktop app can drive the device. Responses are single-line JSON objects;
// log output uses a "[..]" prefix so the host can tell them apart. Call poll()
// every loop to service incoming commands.
//
// Commands (one JSON object per line):
//   {"cmd":"write","materialId":"01001","color":"1200F6","weight":"1KG"}
//   {"cmd":"status"}
//   {"cmd":"dump"}                       -> read blocks 4-6 of a tapped tag
//   {"cmd":"getconfig"} / {"cmd":"setconfig", ...}
//   {"cmd":"getspec"}                    -> firmware-owned UI lists (swatches, etc.)
//   {"cmd":"dbpull","host":"192.168.1.50"}
namespace serialproto {
void poll();
}
