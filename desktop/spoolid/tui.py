"""Textual TUI for the SpoolID writer (parity with the device web UI).

The firmware is the source of truth: this app is a thin client. UI lists (color
swatches, spool sizes, baud rates, log levels) are fetched from the device at
startup via the serial ``getspec`` command, and the firmware builds + encrypts the
payload. All device actions run in worker threads so the UI stays responsive.
"""

import time

from rich.text import Text
from textual import on, work
from textual.app import App, ComposeResult
from textual.containers import Horizontal, VerticalScroll
from textual.widgets import (
    Button,
    Footer,
    Header,
    Input,
    Label,
    Select,
    Static,
    TabbedContent,
    TabPane,
)

from spoolid.material_db import MaterialDb
from spoolid.serial_link import SerialError, SerialLink

# Friendly names for the Creality swatch palette (presentation only; the hex list
# itself comes from the device spec). Unknown hex falls back to "Custom".
COLOR_NAMES = {
    "1200F6": "Blue",
    "3894E1": "Light Blue",
    "FEFF01": "Yellow",
    "F8D531": "Gold",
    "F38E24": "Orange",
    "52D048": "Green",
    "00FEBE": "Teal",
    "B700F3": "Purple",
    "EE301A": "Red",
    "FA5959": "Coral",
    "FFFFFF": "White",
    "D8D8D8": "Light Gray",
    "4C4C4C": "Dark Gray",
    "782543": "Maroon",
    "000000": "Black",
}


def _color_label(hex_value: str) -> Text:
    """Render a swatch row: colored block + friendly name + hex."""
    label = Text()
    label.append("███ ", style=f"#{hex_value}")
    label.append(f"{COLOR_NAMES.get(hex_value.upper(), 'Custom'):<11}")
    label.append(f"#{hex_value}", style="dim")
    return label


class SpoolIdApp(App):
    """Main application."""

    CSS_PATH = "tui.tcss"
    TITLE = "SpoolID"
    BINDINGS = [("q", "quit", "Quit")]

    def __init__(self, db: MaterialDb, link: SerialLink) -> None:
        super().__init__()
        self._db = db
        self._link = link
        self._swatches: list[str] = []  # filled from the device spec

    # ---- layout ----
    def compose(self) -> ComposeResult:
        yield Header()
        with TabbedContent():
            with TabPane("Write", id="tab-write"):
                yield Label("Brand")
                yield Select((), id="brand")
                yield Label("Filament")
                yield Select((), id="filament")
                yield Label("Spool size")
                yield Select((), id="size")
                yield Label("Color")
                with Horizontal(id="color-row"):
                    yield Select((), id="color")
                    yield Static("  ", id="color-preview")
                    yield Input(placeholder="custom hex e.g. 1200F6", id="custom")
                yield Button("Write tag", id="write", variant="primary")
                yield Static("", id="write-status")
            with TabPane("Config", id="tab-config"):
                with VerticalScroll():
                    yield Label("WiFi SSID")
                    yield Input(id="wifiSsid")
                    yield Label("WiFi password (blank = unchanged)")
                    yield Input(id="wifiPass", password=True)
                    yield Label("mDNS hostname")
                    yield Input(id="hostname", placeholder="spoolid")
                    yield Label("AP SSID")
                    yield Input(id="apSsid")
                    yield Label("AP password (blank = unchanged)")
                    yield Input(id="apPass", password=True)
                    yield Label("Log level")
                    yield Select((), id="logLevel")
                    yield Label("Baud rate")
                    yield Select((), id="baud")
                    yield Button("Save settings", id="save")
                    yield Label("Printer host / IP (DB pull)")
                    yield Input(id="printerHost", placeholder="192.168.1.50")
                    yield Button("Pull DB from printer", id="pull")
                    yield Static("", id="config-status")
        yield Footer()

    def on_mount(self) -> None:
        brands = self._db.brands()
        self.query_one("#brand", Select).set_options((b, b) for b in brands)
        if brands:
            self.query_one("#brand", Select).value = brands[0]
            self._load_filaments(brands[0])
        self._load_device()  # fetch spec + config from the device

    # ---- device spec + config (single worker, ordered) ----
    @work(thread=True, exclusive=True, group="serial")
    def _load_device(self) -> None:
        try:
            spec = self._link.send({"cmd": "getspec"})
        except SerialError:
            spec = None
        try:
            cfg = self._link.send({"cmd": "getconfig"})
        except SerialError:
            cfg = None
        if spec is not None:
            self.call_from_thread(self._apply_spec, spec)
        if cfg is not None:
            self.call_from_thread(self._apply_config, cfg)

    def _apply_spec(self, spec: dict) -> None:
        self._swatches = list(spec.get("colorSwatches", []))
        sizes = spec.get("weightLabels", [])
        levels = spec.get("logLevels", [])
        bauds = [str(b) for b in spec.get("baudRates", [])]

        size = self.query_one("#size", Select)
        size.set_options((s, s) for s in sizes)
        if sizes:
            size.value = sizes[0]

        color = self.query_one("#color", Select)
        color.set_options((_color_label(s), s) for s in self._swatches)
        if self._swatches:
            color.value = self._swatches[0]
        self._update_color_preview()

        self.query_one("#logLevel", Select).set_options(
            (lvl, i) for i, lvl in enumerate(levels)
        )
        self.query_one("#baud", Select).set_options((b, b) for b in bauds)

    def _apply_config(self, cfg: dict) -> None:
        self.query_one("#wifiSsid", Input).value = cfg.get("wifiSsid", "")
        self.query_one("#hostname", Input).value = cfg.get("hostname", "")
        self.query_one("#apSsid", Input).value = cfg.get("apSsid", "")
        if "logLevel" in cfg:
            try:
                self.query_one("#logLevel", Select).value = int(cfg["logLevel"])
            except Exception:
                pass
        baud = str(cfg.get("baud", ""))
        try:
            self.query_one("#baud", Select).value = baud
        except Exception:
            pass

    # ---- write tab ----
    @on(Select.Changed, "#brand")
    def _on_brand_changed(self, event: Select.Changed) -> None:
        if event.value is not Select.BLANK:
            self._load_filaments(str(event.value))

    def _load_filaments(self, brand: str) -> None:
        materials = self._db.by_brand(brand)
        options = [(f"{m.name} ({m.type})", m.id) for m in materials]
        select = self.query_one("#filament", Select)
        select.set_options(options)
        if options:
            select.value = options[0][1]

    def _current_color(self) -> str:
        custom = self.query_one("#custom", Input).value.strip().lstrip("#")
        if len(custom) == 6:
            return custom.upper()
        value = self.query_one("#color", Select).value
        if value is not Select.BLANK:
            return str(value)
        return self._swatches[0] if self._swatches else "000000"

    @on(Select.Changed, "#color")
    @on(Input.Changed, "#custom")
    def _update_color_preview(self) -> None:
        try:
            self.query_one("#color-preview", Static).styles.background = f"#{self._current_color()}"
        except Exception:
            pass

    @on(Button.Pressed, "#write")
    def _on_write(self) -> None:
        material_id = self.query_one("#filament", Select).value
        size = self.query_one("#size", Select).value
        if material_id is Select.BLANK or size is Select.BLANK:
            self._set_write_status("pick a filament and size first", error=True)
            return
        color = self._current_color()
        # Firmware builds + encrypts the payload; we only send the selections.
        self._set_write_status("staging - tap a tag on the reader...")
        self._write_worker(str(material_id), color, str(size))

    @work(thread=True, exclusive=True, group="serial")
    def _write_worker(self, material_id: str, color: str, size: str) -> None:
        try:
            resp = self._link.send(
                {"cmd": "write", "materialId": material_id, "color": color, "weight": size}
            )
            if not resp.get("ok"):
                self._post_write_status(f"error: {resp.get('error')}", error=True)
                return
            deadline = time.time() + 30
            while time.time() < deadline:
                time.sleep(0.7)
                status = self._link.send({"cmd": "status"})
                last = status.get("last") or {}
                if not last.get("done"):
                    continue
                if last.get("ok"):
                    enc = " (re-encrypted)" if last.get("encrypted") else ""
                    self._post_write_status(f"written OK  UID {last.get('uid')}{enc}")
                else:
                    self._post_write_status(f"error: {last.get('error')}", error=True)
                return
            self._post_write_status("timed out waiting for tag", error=True)
        except SerialError as err:
            self._post_write_status(f"serial error: {err}", error=True)

    # ---- config tab ----
    @on(Button.Pressed, "#save")
    def _on_save(self) -> None:
        command = {
            "cmd": "setconfig",
            "wifiSsid": self.query_one("#wifiSsid", Input).value,
            "hostname": self.query_one("#hostname", Input).value,
            "apSsid": self.query_one("#apSsid", Input).value,
        }
        log_level = self.query_one("#logLevel", Select).value
        baud = self.query_one("#baud", Select).value
        if log_level is not Select.BLANK:
            command["logLevel"] = int(log_level)
        if baud is not Select.BLANK:
            command["baud"] = int(str(baud))
        wifi_pass = self.query_one("#wifiPass", Input).value
        ap_pass = self.query_one("#apPass", Input).value
        if wifi_pass:
            command["wifiPass"] = wifi_pass
        if ap_pass:
            command["apPass"] = ap_pass
        self._set_config_status("saving...")
        self._setconfig_worker(command)

    @work(thread=True, exclusive=True, group="serial")
    def _setconfig_worker(self, command: dict) -> None:
        try:
            resp = self._link.send(command)
            ok = bool(resp.get("ok"))
            self._post_config_status(
                "saved (reboot device to apply WiFi)" if ok else "save failed",
                error=not ok,
            )
        except SerialError as err:
            self._post_config_status(f"serial error: {err}", error=True)

    @on(Button.Pressed, "#pull")
    def _on_pull(self) -> None:
        host = self.query_one("#printerHost", Input).value.strip()
        if not host:
            self._set_config_status("enter printer host/IP", error=True)
            return
        self._set_config_status("pulling DB from printer...")
        self._pull_worker(host)

    @work(thread=True, exclusive=True, group="serial")
    def _pull_worker(self, host: str) -> None:
        try:
            resp = self._link.send({"cmd": "dbpull", "host": host})
            ok = bool(resp.get("ok"))
            self._post_config_status(
                "DB pulled on device" if ok else f"pull failed: {resp.get('error')}",
                error=not ok,
            )
        except SerialError as err:
            self._post_config_status(f"serial error: {err}", error=True)

    # ---- status helpers (UI thread vs worker thread) ----
    def _set_write_status(self, msg: str, error: bool = False) -> None:
        widget = self.query_one("#write-status", Static)
        widget.update(msg)
        widget.set_class(error, "error")

    def _post_write_status(self, msg: str, error: bool = False) -> None:
        self.call_from_thread(self._set_write_status, msg, error)

    def _set_config_status(self, msg: str, error: bool = False) -> None:
        widget = self.query_one("#config-status", Static)
        widget.update(msg)
        widget.set_class(error, "error")

    def _post_config_status(self, msg: str, error: bool = False) -> None:
        self.call_from_thread(self._set_config_status, msg, error)
