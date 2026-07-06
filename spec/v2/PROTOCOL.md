# SpoolID Wire Protocol — v2

**Protocol:** 2 · **Status:** target (becomes device behavior with the protocol-v2 firmware release)

One logical contract, two transport bindings (HTTP for the device-served web UI, line-JSON
over USB CDC serial for the desktop app). Every payload shape is defined once in
[`schemas/`](schemas/) (JSON Schema 2020-12); this document defines the operations, the
bindings, and the compatibility rules. Fixtures in [`fixtures/`](fixtures/) are golden
replies validated in CI.

## Invariants

- **Firmware is the single source of truth.** AES keys, the 48-char spoolData layout, and
  weight→length codes live only in firmware and are *not part of this spec*. Clients send
  `{materialId, color, weight}`; the firmware builds and encrypts everything. `dump`
  returns opaque hex, uninterpreted.
- UI lists (color swatches, weight labels, baud rates, log levels) are **device-owned
  data**: the spec fixes only their shape; values come from the `spec` operation at runtime.
- Wire mechanics (envelope, error codes, chunk size, enums) are **spec-owned**: firmware
  and every client conform to the values written here.

## Envelope and errors

Every reply on every transport carries `ok`:

- Success: `{"ok": true, ...}`
- Error: `{"ok": false, "error": "<human message>", "code": "<stable code>"}`

`error` is display text and may change; `code` is the programmatic key. Codes:
`bad_json`, `invalid_params`, `no_tag`, `db_failed`, `ota_failed`, `size_required`,
`bad_chunk`, `unknown_cmd`, `internal`.

HTTP status codes (400/404/500) are hints only — clients key off the body envelope,
because serial has no status codes.

## Operations

| Operation | Params schema | Reply schema | Notes |
|---|---|---|---|
| `spec` | — | [`spec.reply`](schemas/spec.reply.schema.json) | UI lists + `version` + `protocol: 2` |
| `read` | — | [`read.reply`](schemas/read.reply.schema.json) | decrypt tapped tag; includes 6-digit `serial` |
| `write` | [`write.params`](schemas/write.params.schema.json) | [`write.reply`](schemas/write.reply.schema.json) | stages a write; result arrives via `status` |
| `status` | — | [`status.reply`](schemas/status.reply.schema.json) | poll for staged-write outcome (`last.done`) |
| `dump` | — | [`dump.reply`](schemas/dump.reply.schema.json) | raw hex of tag blocks, undecrypted semantics |
| `beep` | — | `Ack` (common) | buzzer test |
| `config.get` | — | [`config.reply`](schemas/config.reply.schema.json) | passwords never returned |
| `config.set` | [`config.params`](schemas/config.params.schema.json) | [`configset.reply`](schemas/configset.reply.schema.json) | all fields optional; reboot applies WiFi |
| `db.put` | begin/data per-transport (below) | `db.data.reply` per chunk | client gzips; device stores to LittleFS |
| `ota.flash` | begin/data per-transport (below) | `ota.end.reply` | MD5-verified, A/B rollback, reboots |

Params schemas contain no framing (`cmd`, routes). Bulk transfer (`db.put`, `ota.flash`)
is a logical operation whose chunking mechanics are transport bindings.

## HTTP binding

| Route | Method | Body | Reply |
|---|---|---|---|
| `/api/spec` | GET | — | `spec.reply` |
| `/api/read` | GET | — | `read.reply` · 404 + error(`no_tag`) |
| `/api/dump` | GET | — | `dump.reply` · 404 + error(`no_tag`) |
| `/api/write` | POST | `write.params` JSON | `write.reply` · 400 + error |
| `/api/status` | GET | — | `status.reply` |
| `/api/beep` | GET | — | `Ack` |
| `/api/config` | GET / POST | — / `config.params` JSON | `config.reply` / `configset.reply` |
| `/api/db` | GET / POST | — / raw gzipped bytes (streamed) | gzip stream / `Ack` |
| `/api/ota` | POST | raw image bytes; query `?target=app\|fs&md5=<32hex>` | `Ack` · 500 + error(`ota_failed`) |

HTTP has no `otaabort`: aborting is dropping the connection mid-body.

## Serial binding

- Framing: one JSON object per line. Requests are the params object plus
  `"cmd": "<name>"`. Replies are single-line JSON + `\n`.
- Firmware log lines start with `[` — clients ignore any line not starting with `{`.
- Line endings: CR, LF, or CRLF accepted; input lines are capped at 6144 bytes
  (fits a base64 `OTA_CHUNK` + JSON wrapper).
- Commands: `getspec`, `read`, `write`, `status`, `dump`, `beep`, `getconfig`,
  `setconfig`, and the bulk sessions below.

Bulk sessions (chunks are base64 of at most **`OTA_CHUNK` = 4096** raw bytes):

- `db.put`: `dbbegin {size}` → `dbdata {b}` ×N (each → `db.data.reply {ok, written}`) → `dbend` → `Ack`
- `ota.flash`: `otabegin {size, target, md5?}` → `otadata {b}` ×N (each → `ota.data.reply {ok, written}`) → `otaend` → `ota.end.reply {ok, reboot}` · `otaabort` → `Ack` (serial-only session control)

## Compatibility

- The `spec` reply carries `protocol` (integer) and `version` (firmware semver).
- Clients gate: if `protocol` is present, require equality with the client's supported
  protocol. If absent (pre-v2 firmware), fall back to comparing `major.minor` of
  `version` — see [`../v1/PROTOCOL.md`](../v1/PROTOCOL.md) for what such firmware speaks.
- Change policy: adding an optional reply field = minor release, no protocol bump.
  Changing the envelope, removing/renaming a field, or changing semantics = protocol
  bump + major release.

## Design tokens

Client UIs share the `@spoolid/core` design tokens (`tokens.css`): `--bg`, `--surface`,
`--text`, `--muted`, `--accent`, `--ok`, `--err`, `--border`. Token *names* are part of
this standard; values are themeable (future printer-brand themes swap values only).
