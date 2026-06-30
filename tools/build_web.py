#!/usr/bin/env python3
"""Gzip web/ UI assets into firmware/data/ for the LittleFS image.

Usage:
    .venv/bin/python tools/build_web.py

The async web server serves these *.gz files with Content-Encoding: gzip, so the
ESP32 never has to compress at runtime. Re-run after editing anything in web/.
"""
import gzip
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
DATA = ROOT / "firmware" / "data"

# Assets to ship onto the device filesystem.
ASSETS = ["index.html", "config.html", "app.js", "style.css"]


def main() -> int:
    DATA.mkdir(parents=True, exist_ok=True)
    total_raw = total_gz = 0
    for name in ASSETS:
        src = WEB / name
        if not src.exists():
            print(f"skip (missing): {name}")
            continue
        raw = src.read_bytes()
        blob = gzip.compress(raw, mtime=0)
        (DATA / (name + ".gz")).write_bytes(blob)
        total_raw += len(raw)
        total_gz += len(blob)
        print(f"  {name:<14} {len(raw):>6} -> {len(blob):>6} B")
    print(f"total {total_raw} -> {total_gz} B gz  (data/ ready for `pio run -t uploadfs`)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
