"""USB-CDC link to the firmware: line-JSON commands, single-line JSON replies.

Firmware log lines start with ``[``; only lines starting with ``{`` are treated
as responses. DTR/RTS are deasserted after open so the ESP32-C3 USB-Serial-JTAG
isn't held in reset.
"""

import json
import time

import serial
from serial.tools import list_ports


class SerialError(Exception):
    """Raised when a command gets no JSON response in time."""


class SerialLink:
    """Synchronous request/response link over a serial port."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 8.0) -> None:
        self._timeout = timeout
        self._port = serial.Serial(port, baud, timeout=0.3)
        self._port.dtr = False
        self._port.rts = False
        time.sleep(0.3)
        self._port.reset_input_buffer()

    @staticmethod
    def ports() -> list[str]:
        """Return available serial port device names."""
        return [p.device for p in list_ports.comports()]

    def send(self, command: dict) -> dict:
        """Send a command and wait for the JSON response.

        Args:
            command: Command object; serialized to one JSON line.

        Returns:
            The parsed JSON response.

        Raises:
            SerialError: If no JSON line arrives within the timeout.
        """
        self._port.reset_input_buffer()
        self._port.write((json.dumps(command) + "\n").encode("utf-8"))
        self._port.flush()

        deadline = time.time() + self._timeout
        while time.time() < deadline:
            line = self._port.readline().decode("utf-8", errors="replace").strip()
            if line.startswith("{"):
                return json.loads(line)
        raise SerialError("no JSON response from device")

    def close(self) -> None:
        if self._port.is_open:
            self._port.close()

    def __enter__(self) -> "SerialLink":
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()
