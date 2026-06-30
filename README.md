# SpoolID

Write Creality-format RFID filament tags so a Creality K2/K1 auto-recognizes
third-party or refilled spools. ESP32-C3 + RC522 writer with a built-in web UI and
a cross-platform Python terminal app (Textual) over USB serial.

## Layout

```
firmware/      PlatformIO project (Seeed XIAO ESP32-C3, Arduino)
web/           UI source -> gzipped into firmware/data/ by tools/build_web.py
desktop/       Python 3.14 Textual TUI desktop app (serial control)
tools/         gen_db_header.py (PROGMEM DB), build_web.py (asset gzip)
material_database.json   Creality material DB (default, baked into firmware)
```

### Firmware is the source of truth; web + desktop are thin clients

The firmware owns all data and logic: AES keys, the spoolData format, weight->length
codes, and the UI lists (color swatches, spool sizes, baud rates, log levels). It
builds and encrypts every payload — clients only send the user's selections.

Clients hold **no baked constants**. They fetch the UI lists from the device at
startup and render them:

- web: `GET /api/spec`  (see `web/app.js`, `web/config.html`)
- desktop: serial `{"cmd":"getspec"}`  (see `desktop/spoolid/tui.py`)

The firmware-owned lists live in `firmware/src/ui_spec.cpp`. Behaviour-critical data
(keys/format/codes) stays firmware-only and is never exposed.

## Hardware

| RC522 | XIAO ESP32-C3 |
|-------|---------------|
| 3.3V  | 3.3V |
| GND   | GND |
| SDA/SS| GPIO6 (D4) |
| RST   | GPIO5 (D3) |
| MOSI  | GPIO10 (D10) |
| MISO  | GPIO9 (D9) |
| SCK   | GPIO8 (D8) |
| IRQ   | NC |

Optional active buzzer: set `BUZZER_PIN` in `firmware/src/main.cpp` (default `-1` = off).

## Build & flash firmware

```bash
# 1. regenerate the PROGMEM default DB + gzipped web assets
.venv/bin/python tools/gen_db_header.py
.venv/bin/python tools/build_web.py

# 2. copy secrets template and fill WiFi/AP defaults (optional; configurable later)
cp firmware/include/secrets.h.example firmware/include/secrets.h

# 3. flash code + filesystem
cd firmware
pio run -t upload          # firmware
pio run -t uploadfs        # LittleFS web assets (data/*.gz)
pio device monitor         # serial @115200
```

First boot tries STA WiFi; if it fails it starts the `SpoolID` access point with a
captive portal at the device IP — open it and use the Config page. Once on WiFi the
device is also reachable at `http://spoolid.local` (mDNS hostname, configurable).

## Using it

- **Web:** browse to the device IP. Main page = brand → filament → size → color →
  Write, then tap a blank MIFARE Classic 1K tag on the reader. Config page = DB
  upload/pull, WiFi/AP, log level, baud.
- **Desktop app (Python 3.14 + Textual):**
  ```bash
  python3.14 -m venv .venv && .venv/bin/pip install -r desktop/requirements.txt
  cd desktop
  PYTHONPATH=. ../.venv/bin/python -m spoolid --port /dev/cu.usbmodemXXXX
  # omit --port to be prompted; --db <path> to override the material DB
  ```

## Serial protocol (USB CDC, line-JSON)

Responses are single-line JSON; log lines start with `[`. Commands:

```json
{"cmd":"write","materialId":"01001","color":"1200F6","weight":"1KG"}
{"cmd":"status"}
{"cmd":"dump"}                         // read blocks 4-6 of a tapped tag
{"cmd":"getspec"}                      // firmware-owned UI lists (swatches, sizes...)
{"cmd":"getconfig"} / {"cmd":"setconfig", ...}
{"cmd":"dbpull","host":"192.168.1.50"}
```

## spoolData (48 hex chars)

`AB124` + vendor `0276` + `A2` + filamentId(`1`+base.id) + color(`0`+RRGGBB) +
lengthCode + serial(6) + reserve `000000` + `00000000`.
Length codes: 1KG=0330, 750G=0247, 600G=0198, 500G=0165, 250G=0082.

## Crypto

AES-128-ECB with two fixed keys reverse-engineered from the Creality firmware
(matches the [K2-RFID](https://github.com/DnG-Crafts/K2-RFID) reference project):

- **u_key** — derive the 6-byte MIFARE key: encrypt the UID (replicated to 16 bytes)
  and take the first 6 bytes. The UID is the *plaintext*, not the key.
- **d_key** — encrypt each 16-char spoolData chunk written to blocks 4/5/6.

Both keys live only in `firmware/src/creality_crypto.cpp` (clients never see them).
The reference's hand-rolled AES was verified byte-identical to standard AES-128-ECB,
so the mbedtls implementation here produces the same output the printer expects.
