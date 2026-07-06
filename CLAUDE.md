# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ESP32-C3 + RC522 writer that programs Creality-format MIFARE Classic 1K RFID tags so a
Creality K2/K1 printer auto-recognizes third-party / refilled filament spools. Three
parts: **firmware** (the device), **web** UI (served by the device), **desktop** Wails app
— Go serial backend + Vite/TS web UI (drives the device over USB serial).

## Architecture (the big picture)

**Firmware is the single source of truth for all data and logic.** It owns the AES keys,
the 48-char spoolData format, weight→length codes, and the UI lists (color swatches, spool
sizes, baud rates, log levels). It builds AND encrypts every payload.

**Web and desktop are thin clients** — they only collect the user's selections and send
them. They hold no baked constants: they fetch the UI lists from the device at startup.
Shared client logic (material-DB parsing, read-prefill, status interpretation, color
normalization, compat gate, design tokens) lives in **`spec/ts` (`@spoolid/core`)**, a
pnpm-workspace package imported by both frontends; the workspace root is the repo root
(`pnpm-workspace.yaml`: web, desktop/frontend, spec/ts — one root lockfile).
- web → HTTP (`web_server.cpp`): `/api/spec`, `/api/db`, `/api/write`, `/api/read`, `/api/status`, `/api/config`, `/api/ota`
- desktop → line-JSON over USB CDC (`serial_proto.cpp`): `getspec`, `write`, `read`, `status`, `dump`, `getconfig`, `setconfig`, `dbbegin`/`dbdata`/`dbend`, `otabegin`/`otadata`/`otaend`

OTA: the device self-flashes via ESP `Update` (`ota.cpp`, MD5 + A/B rollback); clients only
relay image bytes (web POST stream, or serial base64 chunks). `getspec` also returns `version`;
the desktop gates compatibility on major.minor. Versioning/releases are automated (release-please
+ the `release` workflow); see the "Releases & over-the-air updates" section in README.

Read: `read` decrypts blocks 4/5/6 (`rfid::readTag` + `crypto::decryptChunk`) → 48-char spoolData,
parsed by `spool::parse` into materialId/color/weight. Clients pre-fill the write form with those
values so the user reviews and writes them back (no dedicated clone path — write always rebuilds).

Both transports are thin shells over the same core: `rfid_writer`, `config`, `material_db`,
`ui_spec`, `spool_data`, `creality_crypto`. When adding a capability, add it to the core and
expose it through *both* `web_server.cpp` and `serial_proto.cpp`.

**The wire contract is specified in `spec/`** (JSON Schema 2020-12 + PROTOCOL.md, versioned:
`spec/v1/` = frozen 1.x behavior, `spec/v2/` = unified target; see issue #18). Validate with
`pnpm --dir spec/ts install && pnpm --dir spec/ts validate`. Any change to a reply shape in
`web_server.cpp` / `serial_proto.cpp` must update the schemas + fixtures in the same PR.

### Crypto (do not get this wrong — it determines whether the printer reads the tag)
`firmware/src/creality_crypto.cpp` — AES-128-ECB with two fixed keys (`U_KEY`, `D_KEY`,
reverse-engineered from the K2-RFID reference: https://github.com/DnG-Crafts/K2-RFID). Mode 0 (`U_KEY`): the UID replicated to 16
bytes is the **plaintext**; encrypt it and take the first 6 bytes as the per-tag MIFARE key
("ekey"). Mode 1 (`D_KEY`): encrypt each 16-char spoolData chunk. The UID is never the key.
Keys live only in firmware; clients never see or compute crypto.

### spoolData (48 hex chars)
`AB124` + `0276` + `A2` + (`1`+5-char material id) + (`0`+RRGGBB) + lengthCode + serial(6) +
`000000` + `00000000`. Built in `spool_data.cpp`.

### Material database
`material_database.json` shape: `result.list[]`, each item has `.base{id,brand,name,meterialType,colors,...}`
(note the misspelling **meterialType**) and `.kvParam{...}`. `tools/gen_db_header.py` gzips it
into a PROGMEM C header shipped as the default; users can replace it by uploading a
`material_database.json` — the client gzips it (browser `CompressionStream` for web,
`compress/gzip` for the desktop app) since the ESP32 has no runtime gzip compressor, then
sends it to `/api/db` (web) or via `dbbegin`/`dbdata`/`dbend` (serial), stored to LittleFS.

## Build / run commands

PlatformIO tooling lives in a repo-root venv (prefer `python3.14`). The desktop app uses
Go + Node + the [Wails CLI](https://wails.io/docs/gettingstarted/installation), not Python.

```bash
# venv (first time; firmware/tools only)
python3.14 -m venv .venv && .venv/bin/pip install platformio

# Firmware (run the two generators FIRST — their outputs are git-ignored)
.venv/bin/python tools/gen_db_header.py          # -> firmware/src/material_db_default.h
.venv/bin/python tools/build_web.py              # web/* -> firmware/data/*.gz
cp firmware/include/secrets.h.example firmware/include/secrets.h   # if missing
.venv/bin/pio run -d firmware                    # compile
.venv/bin/pio run -d firmware -t upload          # flash firmware
.venv/bin/pio run -d firmware -t uploadfs        # flash LittleFS web assets
.venv/bin/pio device monitor -d firmware         # serial @115200

# Desktop app (Wails: Go backend + Vite/TS frontend).
# One-time: install Go 1.23+, Node 26+ with pnpm (npm i -g pnpm), and the Wails CLI:
#   go install github.com/wailsapp/wails/v2/cmd/wails@latest
cd desktop
wails dev            # hot-reload dev window
wails build          # -> desktop/build/bin/SpoolID.app  (== what CI runs)
```

After editing anything in `web/`, re-run `tools/build_web.py` (the device serves the gzipped
copies, not the sources). After editing `material_database.json`, re-run `tools/gen_db_header.py`.

## Gotchas

- **Generated files are git-ignored and required to build**: `firmware/src/material_db_default.h`,
  `firmware/data/*.gz`, `firmware/include/secrets.h`. A fresh checkout won't compile until you
  run `gen_db_header.py` + `build_web.py` and create `secrets.h`. CI does this in `.github/workflows/ci.yml`.
- **If `material_database.json` is lost**, recover it by gunzipping the byte array in the
  generated `firmware/src/material_db_default.h` (content-identical, reformatted compact).
- **`spec/ts/src/generated/` is git-ignored** — TS wire types generated from
  `spec/v2/schemas` by `pnpm --dir spec/ts generate` (the web/desktop build scripts run
  it automatically). A `spec/ts`-only change doesn't dirty Wails' frontend hash cache —
  CI uses `wails build -clean`; locally use `wails build -f` after core changes.
- **Desktop (Wails) generated files are git-ignored** and rebuilt by `wails build`/`wails dev`:
  `desktop/frontend/wailsjs/` (Go↔JS bindings), `desktop/frontend/dist/`, `desktop/build/bin/`,
  and `desktop/frontend/material_database.json` (copied from the repo root by the `copy-db` pnpm
  script, then bundled into the frontend). The Go backend (`app.go`) is **serial-only**; the
  material DB is parsed in the frontend (`frontend/src/db.ts`), like the web client does.
- **Board is XIAO ESP32-C3 with built-in USB-Serial-JTAG** (VID:PID `303A:1001`), not a UART
  bridge. `platformio.ini` sets `monitor_rts=0`/`monitor_dtr=0` so the monitor doesn't hold the
  chip in reset. The idle `loop()` prints nothing — to confirm the device is alive, send
  `{"cmd":"status"}` and expect a JSON reply, or press RST with the monitor open for boot logs.
- **Serial framing**: responses are single-line JSON; firmware log lines are prefixed with `[`.
  Clients must ignore non-`{` lines. Commands accept CR, LF, or CRLF line endings.
- Firmware pins (RC522↔XIAO): SS=GPIO6, RST=GPIO5, SCK=GPIO21(D6), MISO=GPIO20(D7), MOSI=GPIO10.
  SCK/MISO deliberately avoid GPIO8/GPIO9 — those are C3 boot strapping pins; wiring
  the RC522 there holds the chip in download mode at power-up (manual reset needed to boot).
