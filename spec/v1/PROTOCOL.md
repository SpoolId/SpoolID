# SpoolID Wire Protocol — v1 (frozen)

**Status:** frozen snapshot of what 1.x firmware actually speaks. Errata-only after
merge; new work happens in [`../v2/PROTOCOL.md`](../v2/PROTOCOL.md). This is the contract
a v2 client falls back to when `getspec` returns no `protocol` field.

v1 was never designed as one contract — it is two hand-written transports that drifted.
The schemas in [`schemas/`](schemas/) describe the union faithfully (relaxed where the
transports disagree), and the table below pins the divergences. Fixtures in
[`fixtures/`](fixtures/) are captured from a device running 1.x firmware.

## Shared behavior

- Success replies carry `ok: true` **on serial always; on HTTP only for some routes**
  (see divergences). Error replies are `{"ok": false, "error": "<message>"}` on both —
  no `code` field in v1.
- Operations and shapes otherwise match v2 minus: no `protocol` in the spec reply, no
  `serial` in the read reply, `dbdata` acks without `written`.
- Serial framing identical to v2: line-JSON, `[`-prefixed log lines ignored, CR/LF/CRLF,
  6144-byte line cap, base64 chunks ≤ 4096 raw bytes.

## Transport divergence table

| # | Divergence | HTTP (web_server.cpp) | Serial (serial_proto.cpp) |
|---|---|---|---|
| 1 | `ok` on read-style replies | **omitted** on `GET /api/spec`, `/api/status`, `/api/config` | always present (`getspec`, `status`, `getconfig`) |
| 2 | `dump` | not available | `{"cmd":"dump"}` |
| 3 | `otaabort` | not available (drop the connection) | `{"cmd":"otaabort"}` |
| 4 | DB upload | single POST of pre-gzipped bytes, streamed to LittleFS; **always replies `{"ok":true}`**, even if the write failed | `dbbegin` → `dbdata {b}` ×N → `dbend`, each chunk acked `{"ok":true}` |
| 5 | `dbbegin` `size` | n/a | desktop sends `{size}`; **firmware ignores it** (v1 schema: optional) |
| 6 | OTA parameters | query string `?target=app\|fs&md5=<32hex>`, raw streamed body | JSON body `{size, target, md5?}`, base64 chunks; `otadata` replies include `written` |
| 7 | Error transport | HTTP status 400/404/500 + body | `ok:false` body only |
| 8 | `write.params` color | leading `#` accepted (firmware trims) | same — but desktop always sends bare hex |

## Operations (as shipped in 1.x)

`spec` (`GET /api/spec` / `getspec`), `read`, `write`, `status`, `beep`,
`config.get`/`config.set`, `db.put`, `ota.flash`, plus serial-only `dump` and `otaabort`.
Route table and command names are identical to v2 except: no `GET /api/dump`.

## Compatibility

`getspec`/`/api/spec` returns `version` (firmware semver) only. Desktop 1.x gates on
matching `major.minor`; the web UI ships on the device's own flash and is inherently
matched. Dev builds report `0.0.0-dev` and skip the gate.
