# Changelog

## [2.0.0](https://github.com/SpoolId/SpoolID/compare/v1.0.0...v2.0.0) (2026-07-06)


### ⚠ BREAKING CHANGES

* wire replies are additive on the wire, but the contract now requires the ok envelope everywhere and code on every error; clients should gate on the new protocol field.

### Features

* **spec:** versioned wire-contract spec (v1 as-is + v2 target) ([6077197](https://github.com/SpoolId/SpoolID/commit/6077197dbadac12f5978b201d5886302a515c8e9)), closes [#18](https://github.com/SpoolId/SpoolID/issues/18)
* unify wire protocol (v2 envelope, error codes, protocol field) ([1998b0b](https://github.com/SpoolId/SpoolID/commit/1998b0b48e855f1d21b529fd403c562dbe12e75c)), closes [#18](https://github.com/SpoolId/SpoolID/issues/18)

## 1.0.0 (2026-07-02)


### Features

* **desktop:** SpoolID desktop app (Wails — Go + Vite/TS) ([d527ce6](https://github.com/mateusdemboski/SpoolID/commit/d527ce6c2634e9f9a870fdb09a829497cdcbb0a0))
* **firmware:** Creality RFID spool tag writer (ESP32-C3 + RC522) ([6aab754](https://github.com/mateusdemboski/SpoolID/commit/6aab75493cc7d95ccf93256b2281068e15b0128d))
* **web:** device web UI (Vite + TypeScript) ([540b951](https://github.com/mateusdemboski/SpoolID/commit/540b95191df2b9b4cdb655d0b2d8584daf3684fd))


### Documentation

* project docs, license, and gitignore ([ed0746f](https://github.com/mateusdemboski/SpoolID/commit/ed0746fa3b4c2f6e60d887b9dedc71274a37b513))
