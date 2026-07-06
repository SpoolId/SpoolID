#!/usr/bin/env python3
"""Capture wire-protocol fixtures from a live SpoolID device.

Overwrites the hand-authored fixtures with the device's actual replies so CI
validates against reality. Read-only operations only (spec/status/config/read/
dump) — nothing is written to tags or flash.

Usage:
    python spec/scripts/capture_fixtures.py --version v1 --host 192.168.1.50
    python spec/scripts/capture_fixtures.py --version v1 --port /dev/cu.usbmodem101
    (pass both to capture both transports; hold a tag on the reader for read/dump
     "ok" cases, remove it for the err-no-tag case when prompted)

Serial capture needs pyserial (already in the PlatformIO venv):
    .venv/bin/python spec/scripts/capture_fixtures.py ...
"""
import argparse
import json
import sys
import urllib.request
from pathlib import Path

SPEC = Path(__file__).resolve().parent.parent


def save(version: str, stem: str, case: str, payload: dict) -> None:
    out = SPEC / version / "fixtures" / stem / f"{case}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2) + "\n")
    print(f"  {out.relative_to(SPEC)}")


def http_get(host: str, path: str) -> dict:
    with urllib.request.urlopen(f"http://{host}{path}", timeout=10) as r:
        return json.load(r)


def capture_http(version: str, host: str) -> None:
    print(f"HTTP capture from {host}:")
    suffix = "-http" if version == "v1" else ""
    save(version, "spec.reply", f"ok{suffix}", http_get(host, "/api/spec"))
    save(version, "status.reply", f"idle{suffix}", http_get(host, "/api/status"))
    save(version, "config.reply", f"get{suffix}", http_get(host, "/api/config"))
    input("hold a tag on the reader, then press Enter... ")
    save(version, "read.reply", "ok", http_get(host, "/api/read"))
    input("remove the tag, then press Enter... ")
    try:
        http_get(host, "/api/read")
    except urllib.error.HTTPError as e:  # 404 carries the error body
        save(version, "read.reply", "err-no-tag", json.load(e))


def send_serial(ser, cmd: dict) -> dict:
    ser.reset_input_buffer()
    ser.write((json.dumps(cmd) + "\n").encode())
    while True:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            raise TimeoutError(f"no reply to {cmd}")
        if line.startswith("{"):  # log lines start with '['
            return json.loads(line)


def capture_serial(version: str, port: str) -> None:
    import serial  # pyserial, from the PlatformIO venv

    print(f"serial capture from {port}:")
    with serial.Serial(port, 115200, timeout=10) as ser:
        ser.dtr = False
        ser.rts = False
        suffix = "-serial" if version == "v1" else ""
        save(version, "spec.reply", f"ok{suffix}", send_serial(ser, {"cmd": "getspec"}))
        save(version, "status.reply", f"idle{suffix}", send_serial(ser, {"cmd": "status"}))
        save(version, "config.reply", f"get{suffix}", send_serial(ser, {"cmd": "getconfig"}))
        input("hold a tag on the reader, then press Enter... ")
        save(version, "read.reply", "ok", send_serial(ser, {"cmd": "read"}))
        save(version, "dump.reply", "ok", send_serial(ser, {"cmd": "dump"}))
        input("remove the tag, then press Enter... ")
        save(version, "read.reply", "err-no-tag", send_serial(ser, {"cmd": "read"}))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--version", choices=["v1", "v2"], required=True)
    ap.add_argument("--host", help="device IP for HTTP capture")
    ap.add_argument("--port", help="serial port for serial capture")
    args = ap.parse_args()
    if not args.host and not args.port:
        ap.error("need --host and/or --port")
    if args.host:
        capture_http(args.version, args.host)
    if args.port:
        capture_serial(args.version, args.port)
    print("done — review config.reply fixtures for private values (WiFi SSID, hostname),")
    print("then run `pnpm --dir spec/ts validate` and commit the diff")
    return 0


if __name__ == "__main__":
    sys.exit(main())
