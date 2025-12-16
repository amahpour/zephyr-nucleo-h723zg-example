"""
Physical UART-based DUT connection.
"""

import time

import serial

from .base import DUTBase


class PhysicalDevice(DUTBase):
    """Physical UART connection to real hardware."""

    def __init__(
        self,
        port: str,
        baudrate: int = 115200,
        timeout: float = 2.0,
    ):
        """
        Initialize physical device connection.

        Args:
            port: Serial port path (e.g., /dev/ttyACM0, COM3)
            baudrate: Serial baud rate
            timeout: Serial read timeout in seconds
        """
        self._port = port
        self._baudrate = baudrate
        self._timeout = timeout
        self._serial: serial.Serial = None

    def start(self) -> None:
        """Open serial connection to the device."""
        if not self._port:
            raise ValueError("Serial port not specified")

        self._serial = serial.Serial(
            self._port,
            baudrate=self._baudrate,
            timeout=self._timeout,
        )
        time.sleep(0.5)  # Give device time to initialize

        # Clear any buffered data
        self._serial.reset_input_buffer()
        time.sleep(0.5)

    def stop(self) -> None:
        """Close serial connection."""
        if self._serial:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None

    def send_command(self, cmd: str, timeout: float = 1.0) -> str:
        """
        Send a command and wait for the shell prompt to return.

        Args:
            cmd: Command string to send
            timeout: Timeout in seconds to wait for response

        Returns:
            Response string from the device
        """
        if not self._serial:
            raise RuntimeError("Serial connection not established. Call start() first.")

        self._serial.reset_input_buffer()
        self._serial.write(f"{cmd}\r\n".encode())

        response = b""
        start = time.time()
        while time.time() - start < timeout:
            if self._serial.in_waiting:
                response += self._serial.read(self._serial.in_waiting)
                if b"uart:~$" in response:
                    break
            time.sleep(0.05)

        return response.decode(errors="replace")

    def __enter__(self):
        """Context manager entry."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.stop()
        return False
