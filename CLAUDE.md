# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ESP32-C3 + RC522 writer that programs Creality-format MIFARE Classic 1K RFID tags so a
Creality K2/K1 printer auto-recognizes third-party / refilled filament spools. Three
parts: **firmware** (the device), **web** UI (served by the device), **desktop** Python
TUI (drives the device over USB serial).

## Architecture (the big picture)

**Firmware is the single source of truth for all data and logic.** It owns the AES keys,
the 48-char spoolData format, weight→length codes, and the UI lists (color swatches, spool
sizes, baud rates, log levels). It builds AND encrypts every payload.

**Web and desktop are thin clients** — they only collect the user's selections and send
them. They hold no baked constants: they fetch the UI lists from the device at startup.
- web → HTTP (`web_server.cpp`): `/api/spec`, `/api/db`, `/api/write`, `/api/status`, `/api/config`, `/api/db/pull`
- desktop → line-JSON over USB CDC (`serial_proto.cpp`): `getspec`, `write`, `status`, `dump`, `getconfig`, `setconfig`, `dbpull`

Both transports are thin shells over the same core: `rfid_writer`, `config`, `material_db`,
`ui_spec`, `spool_data`, `creality_crypto`. When adding a capability, add it to the core and
expose it through *both* `web_server.cpp` and `serial_proto.cpp`.

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
into a PROGMEM C header shipped as the default; users can replace it via upload (browser gzips
with CompressionStream) or printer-pull (firmware fetches raw, stores uncompressed — the ESP32
has no runtime gzip compressor).

## Build / run commands

PlatformIO and the Python tooling live in a repo-root venv. Prefer `python3.14`.

```bash
# venv (first time)
python3.14 -m venv .venv && .venv/bin/pip install platformio -r desktop/requirements.txt

# Firmware (run the two generators FIRST — their outputs are git-ignored)
.venv/bin/python tools/gen_db_header.py          # -> firmware/src/material_db_default.h
.venv/bin/python tools/build_web.py              # web/* -> firmware/data/*.gz
cp firmware/include/secrets.h.example firmware/include/secrets.h   # if missing
.venv/bin/pio run -d firmware                    # compile
.venv/bin/pio run -d firmware -t upload          # flash firmware
.venv/bin/pio run -d firmware -t uploadfs        # flash LittleFS web assets
.venv/bin/pio device monitor -d firmware         # serial @115200

# Desktop app
cd desktop && PYTHONPATH=. ../.venv/bin/python -m spoolid --port /dev/cu.usbmodemXXXX
# omit --port to be prompted

# Desktop checks (what CI runs — no test suite)
.venv/bin/python -m compileall desktop/spoolid
PYTHONPATH=desktop .venv/bin/python -c "import spoolid.tui"
```

After editing anything in `web/`, re-run `tools/build_web.py` (the device serves the gzipped
copies, not the sources). After editing `material_database.json`, re-run `tools/gen_db_header.py`.

## Gotchas

- **Generated files are git-ignored and required to build**: `firmware/src/material_db_default.h`,
  `firmware/data/*.gz`, `firmware/include/secrets.h`. A fresh checkout won't compile until you
  run `gen_db_header.py` + `build_web.py` and create `secrets.h`. CI does this in `.github/workflows/ci.yml`.
- **If `material_database.json` is lost**, recover it by gunzipping the byte array in the
  generated `firmware/src/material_db_default.h` (content-identical, reformatted compact).
- **Board is XIAO ESP32-C3 with built-in USB-Serial-JTAG** (VID:PID `303A:1001`), not a UART
  bridge. `platformio.ini` sets `monitor_rts=0`/`monitor_dtr=0` so the monitor doesn't hold the
  chip in reset. The idle `loop()` prints nothing — to confirm the device is alive, send
  `{"cmd":"status"}` and expect a JSON reply, or press RST with the monitor open for boot logs.
- **Serial framing**: responses are single-line JSON; firmware log lines are prefixed with `[`.
  Clients must ignore non-`{` lines. Commands accept CR, LF, or CRLF line endings.
- Firmware pins (RC522↔XIAO): SS=GPIO6, RST=GPIO5, SCK=GPIO8, MISO=GPIO9, MOSI=GPIO10.
