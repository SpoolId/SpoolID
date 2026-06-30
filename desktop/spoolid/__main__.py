"""Entry point: ``python -m spoolid [--port ...] [--baud ...] [--db ...]``."""

import argparse
import sys
from pathlib import Path

from spoolid.material_db import MaterialDb
from spoolid.serial_link import SerialLink
from spoolid.tui import SpoolIdApp


def _find_db() -> str:
    """Locate material_database.json (cwd, then repo root)."""
    candidates = [
        Path("material_database.json"),
        Path(__file__).resolve().parents[2] / "material_database.json",
    ]
    for path in candidates:
        if path.exists():
            return str(path)
    return "material_database.json"


def _pick_port() -> str | None:
    """Prompt the user to choose a serial port (auto-select if only one)."""
    ports = SerialLink.ports()
    if not ports:
        return None
    if len(ports) == 1:
        return ports[0]
    print("Serial ports:")
    for i, port in enumerate(ports):
        print(f"  [{i}] {port}")
    choice = input("select: ").strip()
    if choice.isdigit() and 0 <= int(choice) < len(ports):
        return ports[int(choice)]
    return ports[0]


def main() -> int:
    parser = argparse.ArgumentParser(prog="spoolid", description="SpoolID writer")
    parser.add_argument("--port", help="serial port (prompts if omitted)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--db", help="path to material_database.json")
    args = parser.parse_args()

    db_path = args.db or _find_db()
    try:
        db = MaterialDb.load(db_path)
    except (OSError, KeyError, ValueError) as err:
        print(f"failed to load material DB ({db_path}): {err}", file=sys.stderr)
        print("pass --db <path to material_database.json>", file=sys.stderr)
        return 1

    port = args.port or _pick_port()
    if port is None:
        print("no serial port available.", file=sys.stderr)
        return 1

    try:
        link = SerialLink(port, args.baud)
    except Exception as err:  # pyserial raises a variety of errors
        print(f"failed to open {port}: {err}", file=sys.stderr)
        return 1

    try:
        SpoolIdApp(db, link).run()
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
