# SpoolID

Write Creality-format RFID filament tags so a Creality K2/K1 auto-recognizes
third-party or refilled spools. ESP32-C3 + RC522 writer with a built-in web UI and
a cross-platform desktop app (Wails — Go backend + web UI) over USB serial.

## Layout

```
firmware/      PlatformIO project (Seeed XIAO ESP32-C3, Arduino)
web/           UI source -> gzipped into firmware/data/ by tools/build_web.py
desktop/       Wails desktop app — Go serial backend + Vite/TS web UI
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
- desktop: serial `{"cmd":"getspec"}`  (see `desktop/frontend/src/main.ts`)

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
| MISO  | GPIO20 (D7) |
| SCK   | GPIO21 (D6) |
| IRQ   | NC |

> SCK/MISO avoid GPIO8/GPIO9 — those are ESP32-C3 boot strapping pins; wiring the
> RC522 to them holds the chip in download mode at power-up (needs a manual reset
> to start). D6/D7 are safe.

Optional active buzzer on `BUZZER_PIN` in `firmware/src/main.cpp` (default `2` = D0; set `-1` to disable).

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
  Write, then tap a blank MIFARE Classic 1K tag on the reader. **Read** decrypts a
  tapped tag and pre-fills the form with its material/color/weight so you can review
  and Write it back. Config page = DB upload/pull, WiFi/AP, log level, baud.
- **Desktop app (Wails — Go + web UI):** pick a serial port and **Connect** in the
  window, then use the Write / Config views (same brand → filament → size → color →
  Write flow, plus Read pre-fill). Prerequisites: [Go](https://go.dev/dl/) 1.23+,
  [Node](https://nodejs.org/) 18+ with **pnpm** (`corepack enable`), and the
  [Wails CLI](https://wails.io/docs/gettingstarted/installation)
  (`go install github.com/wailsapp/wails/v2/cmd/wails@latest`).
  ```bash
  cd desktop
  wails dev            # hot-reload dev window
  wails build          # -> desktop/build/bin/SpoolID.app (.exe / binary on other OSes)
  ```
  `wails build` produces a single native binary per platform (macOS `.app`, Windows
  `.exe`, Linux ELF) — no runtime, no bundled interpreter. The material DB is copied
  from the repo root into `desktop/frontend/` at build time (`copy-db` pnpm script)
  and bundled into the frontend.

## Wire protocol

The full contract — operations, JSON shapes, HTTP + serial bindings, error codes,
compatibility rules — lives in **[`spec/v2/PROTOCOL.md`](spec/v2/PROTOCOL.md)**
(JSON Schemas in [`spec/v2/schemas/`](spec/v2/schemas/), validated in CI against
fixtures produced by the firmware's own reply builders). Quick taste, over USB
serial (single-line JSON; log lines start with `[`):

```json
{"cmd":"getspec"}   // UI lists + firmware version + protocol generation
{"cmd":"write","materialId":"01001","color":"1200F6","weight":"1KG"}
{"cmd":"status"}    // poll for the staged-write outcome
```

Every reply carries `ok`; errors add a stable `code` (`no_tag`, `invalid_params`, …).
Clients gate compatibility on the `protocol` field; older 1.x firmware (documented in
[`spec/v1/PROTOCOL.md`](spec/v1/PROTOCOL.md)) is recognized via major.minor fallback.

## Releases & over-the-air updates

Versioning is automated from Conventional Commits (Angular). `release-please`
maintains a Release PR (version bump + CHANGELOG); merging it tags `vX.Y.Z` and the
Release workflow attaches firmware (`firmware.bin`, `littlefs.bin` + checksums) and
desktop builds (macOS/Windows/Linux; the Linux/Windows binaries are UPX-compressed,
macOS is not — the OS rejects packed binaries). Firmware and desktop share one
**lockstep version** and must match on the minor — the desktop warns on a mismatch.

Updating a device (no cable re-flash after the first install):

- **Desktop:** Config → *Firmware update* — paste the release asset URL
  (`firmware.bin` or `littlefs.bin`), pick the target, Flash. The app downloads it
  and streams it to the device over serial.
- **Web:** Config page → *Firmware update* — select a `.bin` and upload (or
  `POST /api/ota?target=app|fs&md5=<hex>` directly with the raw image body).

Either way the device does the flashing itself via ESP `Update` (MD5-verified, A/B
rollback), then reboots. Clients only relay bytes — no TLS/esptool on the MCU.

> First install and any partition-table change still need a one-time USB flash
> (`pio run -t upload` + `-t uploadfs`), which wipes NVS + LittleFS.

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
