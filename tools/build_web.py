#!/usr/bin/env python3
"""Build the web/ UI (Vite) and gzip the output into firmware/data/ for LittleFS.

Runs `pnpm build` in web/ (Vite: TypeScript -> minified bundles), then gzips
every file in web/dist into firmware/data/*.gz. The async web server serves
these with Content-Encoding: gzip, so the ESP32 never compresses at runtime.

Usage:
    python tools/build_web.py               # build (pnpm) + gzip
    python tools/build_web.py --skip-build  # gzip an existing web/dist only
"""
import argparse
import gzip
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
DIST = WEB / "dist"
DATA = ROOT / "firmware" / "data"


def build_web() -> None:
    """Run the Vite production build via pnpm."""
    pnpm = shutil.which("pnpm")
    if not pnpm:
        sys.exit("pnpm not found — install Node + run `corepack enable`, or pass --skip-build")
    # Root pnpm workspace: installs web + spec/ts (the shared @spoolid/core).
    subprocess.run([pnpm, "--dir", str(ROOT), "install", "--prefer-offline"], check=True)
    subprocess.run([pnpm, "--dir", str(WEB), "build"], check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build + gzip the web UI")
    ap.add_argument("--skip-build", action="store_true", help="gzip existing web/dist only")
    args = ap.parse_args()

    if not args.skip_build:
        build_web()
    if not DIST.exists():
        sys.exit(f"missing {DIST} — run without --skip-build first")

    DATA.mkdir(parents=True, exist_ok=True)
    for stale in DATA.glob("*.gz"):
        stale.unlink()

    total_raw = total_gz = 0
    for src in sorted(DIST.rglob("*")):
        if not src.is_file():
            continue
        raw = src.read_bytes()
        blob = gzip.compress(raw, mtime=0)
        (DATA / (src.name + ".gz")).write_bytes(blob)
        total_raw += len(raw)
        total_gz += len(blob)
        print(f"  {src.name:<14} {len(raw):>6} -> {len(blob):>6} B")
    print(f"total {total_raw} -> {total_gz} B gz  (data/ ready for `pio run -t uploadfs`)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
