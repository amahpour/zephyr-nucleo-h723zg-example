"""
QEMU-based DUT with automatic PTY detection.
"""

import os
import re
import subprocess
import time
from pathlib import Path

import serial

from .base import DUTBase


class QEMUDevice(DUTBase):
    """QEMU-based DUT with automatic PTY detection."""

    def __init__(
        self,
        kernel_path: str = None,
        boot_timeout: float = 5.0,
        zephyr_sdk_path: str = None,
    ):
        """
        Initialize QEMU device.

        Args:
            kernel_path: Path to the kernel ELF file. If None, uses default build location.
            boot_timeout: Timeout in seconds to wait for QEMU to boot.
            zephyr_sdk_path: Path to Zephyr SDK. If None, uses ZEPHYR_SDK_INSTALL_DIR or default.
        """
        self._kernel_path = kernel_path
        self._boot_timeout = boot_timeout
        self._zephyr_sdk_path = zephyr_sdk_path
        self._process: subprocess.Popen = None
        self._serial: serial.Serial = None
        self._pty_path: str = None

    def _find_qemu_binary(self) -> str:
        """Find the QEMU binary path."""
        zephyr_sdk = self._zephyr_sdk_path or os.environ.get(
            "ZEPHYR_SDK_INSTALL_DIR", os.path.expanduser("~/zephyr-sdk-0.17.4")
        )
        qemu_bin = f"{zephyr_sdk}/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-system-i386"

        if not os.path.exists(qemu_bin):
            raise FileNotFoundError(f"QEMU binary not found at {qemu_bin}")

        return qemu_bin

    def _find_kernel(self) -> Path:
        """Find the kernel ELF file."""
        repo_root = Path(__file__).parent.parent.parent.parent

        if self._kernel_path:
            kernel = Path(self._kernel_path)
            if not kernel.is_absolute():
                # Relative to repo root
                kernel = repo_root / kernel
            if not kernel.exists():
                raise FileNotFoundError(f"Kernel not found at {kernel}. Build the app first.")
            return kernel

        # Try default locations in order of preference
        default_paths = [
            repo_root / "build-qemu" / "zephyr" / "zephyr.elf",  # QEMU-specific build
            repo_root / "build" / "zephyr" / "zephyr.elf",  # Generic build
        ]

        for kernel in default_paths:
            if kernel.exists():
                return kernel

        raise FileNotFoundError(
            f"Kernel not found. Tried: {[str(p) for p in default_paths]}. "
            "Build the app with 'west build -b qemu_x86 app' first."
        )

    def start(self) -> None:
        """Start QEMU and establish serial connection."""
        qemu_bin = self._find_qemu_binary()
        kernel = self._find_kernel()

        # Start QEMU with PTY
        cmd = [
            qemu_bin,
            "-m", "32",
            "-cpu", "qemu32,+nx,+pae",
            "-machine", "q35",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-no-reboot",
            "-nographic",
            "-machine", "acpi=off",
            "-net", "none",
            "-serial", "pty",
            "-kernel", str(kernel),
        ]

        self._process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        # Wait for QEMU to print the PTY path
        self._pty_path = None
        timeout_iterations = int(self._boot_timeout * 10)  # 100ms per iteration
        for _ in range(timeout_iterations):
            line = self._process.stdout.readline()
            if not line:
                time.sleep(0.1)
                continue

            # Look for: char device redirected to /dev/pts/X
            match = re.search(r"/dev/pts/\d+", line)
            if match:
                self._pty_path = match.group(0)
                break

        if not self._pty_path:
            self._cleanup_process()
            raise RuntimeError("QEMU did not output PTY path")

        # Wait for PTY to be ready and QEMU to fully boot
        time.sleep(0.5)  # Wait for PTY to be accessible
        time.sleep(2)  # Wait for firmware to boot

        # Open serial connection
        self._serial = serial.Serial(self._pty_path, baudrate=115200, timeout=2)
        time.sleep(0.5)

        # Clear any initial output
        self._serial.reset_input_buffer()
        time.sleep(1)

    def stop(self) -> None:
        """Stop QEMU and close serial connection."""
        if self._serial:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None

        self._cleanup_process()

    def _cleanup_process(self) -> None:
        """Clean up QEMU process."""
        if self._process:
            self._process.terminate()
            try:
                self._process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
            self._process = None

    def send_command(self, cmd: str, timeout: float = 1.0) -> str:
        """
        Send a command and wait for the shell prompt to return.

        Args:
            cmd: Command string to send
            timeout: Timeout in seconds to wait for response

        Returns:
            Response string from the DUT
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
