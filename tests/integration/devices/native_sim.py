"""
native_sim DUT driving a virtual PCB.

Unlike the QEMU DUT this needs no SDK lookup and no PTY discovery: the
firmware is an ordinary host executable and its console is on stdin/stdout
(CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT).

Starting the DUT means starting a small constellation of processes -- the
board, one process per IC, then the firmware -- because on this target the
chips genuinely live outside the MCU. Each instance gets its own socket so
concurrent runs cannot see each other's traffic.
"""

import os
import re
import signal
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from .base import DUTBase

PROMPT = "uart:~$"


class NativeSimDevice(DUTBase):
    """Virtual PCB DUT: firmware, board and IC models as separate processes."""

    def __init__(
        self,
        binary: str = None,
        vpcb_dir: str = None,
        netlist: str = None,
        dacs=None,
        boot_timeout: float = 10.0,
        sock: str = None,
    ):
        """
        Args:
            binary: Path to zephyr.exe, relative to the repo root if not absolute.
            vpcb_dir: Directory holding vpcb-board and vpcb-dac7578.
            netlist: Netlist file passed to the board process.
            dacs: I2C addresses to instantiate a DAC model for, e.g. ["0x48", "0x4c"].
            boot_timeout: Seconds to wait for the shell prompt after start.
            sock: Explicit socket path. Defaults to a unique temporary path.
        """
        repo_root = Path(__file__).parent.parent.parent.parent

        self._repo_root = repo_root
        self._binary = self._resolve(binary or "build-vpcb-fw/zephyr/zephyr.exe")
        self._vpcb_dir = self._resolve(vpcb_dir or "build-vpcb")
        self._netlist = self._resolve(netlist or "vpcb/netlists/adc_loopback.txt")
        self._dacs = [str(a) for a in (dacs or ["0x48", "0x4c"])]
        self._boot_timeout = boot_timeout

        # A per-instance socket: two suites running at once must not collide.
        self._sock = sock or os.path.join(
            tempfile.gettempdir(), f"vpcb-{os.getpid()}-{id(self):x}.sock"
        )

        self._helpers = []          # board + IC processes
        self._fw = None             # firmware process
        self._buf = ""              # console text accumulated by the reader thread
        self._lock = threading.Lock()
        self._reader = None
        self._stop_reader = threading.Event()

    def _resolve(self, path: str) -> Path:
        p = Path(path)
        return p if p.is_absolute() else self._repo_root / p

    # ------------------------------------------------------------------ setup

    def _spawn_helper(self, argv):
        """Start a board or IC process in its own process group."""
        proc = subprocess.Popen(
            argv,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        self._helpers.append(proc)
        return proc

    def _pump(self):
        """Drain the firmware's console into a buffer until told to stop.

        Raw reads, not readline(): the shell prompt is written without a
        trailing newline, so a line-oriented read blocks forever waiting for
        a newline that only arrives with the *next* command.
        """
        fd = self._fw.stdout.fileno()
        while not self._stop_reader.is_set():
            try:
                chunk = os.read(fd, 4096)
            except (OSError, ValueError):
                break
            if not chunk:
                break
            with self._lock:
                self._buf += chunk.decode(errors="replace")

    def _start(self) -> None:
        board = self._vpcb_dir / "vpcb-board"
        dac = self._vpcb_dir / "vpcb-dac7578"

        for required in (self._binary, board, dac):
            if not required.exists():
                raise FileNotFoundError(
                    f"{required} not found. Build both halves first:\n"
                    f"  cmake -S vpcb -B build-vpcb && cmake --build build-vpcb\n"
                    f"  west build -b native_sim app -d build-vpcb-fw"
                )

        if os.path.exists(self._sock):
            os.unlink(self._sock)

        # The board owns the netlist, so it has to be listening before any
        # IC or the firmware tries to attach.
        self._spawn_helper([str(board), "--sock", self._sock,
                            "--netlist", str(self._netlist)])
        self._wait_for_socket()

        for addr in self._dacs:
            self._spawn_helper([str(dac), "--sock", self._sock, "--addr", addr, "-q"])
        time.sleep(0.3)  # let the ICs announce themselves and drive their nets

        self._fw = subprocess.Popen(
            [str(self._binary), f"--vpcb-sock={self._sock}"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self._stop_reader.clear()
        self._reader = threading.Thread(target=self._pump, daemon=True)
        self._reader.start()

        self._wait_for_prompt()

    def start(self) -> None:
        """Bring up the whole stack, leaving nothing behind if it fails.

        pytest's fixture only calls stop() after a successful yield, so a
        partially-started stack would otherwise leak a board and two IC
        processes per attempt.
        """
        try:
            self._start()
        except Exception:
            self.stop()
            raise

    def _wait_for_socket(self, timeout: float = 5.0) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if os.path.exists(self._sock):
                return
            time.sleep(0.05)
        raise RuntimeError(f"virtual PCB board did not create {self._sock}")

    def _wait_for_prompt(self) -> None:
        deadline = time.time() + self._boot_timeout
        while time.time() < deadline:
            with self._lock:
                if PROMPT in self._buf:
                    self._buf = ""
                    return
            if self._fw.poll() is not None:
                raise RuntimeError(
                    f"firmware exited during boot (rc={self._fw.returncode}):\n{self._buf}"
                )
            time.sleep(0.05)
        raise RuntimeError(f"no shell prompt within {self._boot_timeout}s:\n{self._buf}")

    # --------------------------------------------------------------- teardown

    @staticmethod
    def _kill(proc) -> None:
        if proc is None or proc.poll() is not None:
            return
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            return
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
            proc.wait(timeout=2)

    def stop(self) -> None:
        """Tear down every process. Safe to call after a failed start()."""
        self._stop_reader.set()

        self._kill(self._fw)
        if self._fw is not None:
            for stream in (self._fw.stdin, self._fw.stdout):
                try:
                    stream.close()
                except Exception:
                    pass
            self._fw = None

        # ICs and board last: killing the board first makes the ICs log noise.
        for proc in reversed(self._helpers):
            self._kill(proc)
        self._helpers = []

        if self._reader is not None:
            self._reader.join(timeout=2)
            self._reader = None

        try:
            os.unlink(self._sock)
        except OSError:
            pass

    # ---------------------------------------------------------------- console

    def send_command(self, cmd: str, timeout: float = 2.0) -> str:
        """Send a shell command and return everything printed up to the prompt."""
        if self._fw is None or self._fw.poll() is not None:
            raise RuntimeError("firmware is not running. Call start() first.")

        with self._lock:
            self._buf = ""

        self._fw.stdin.write(f"{cmd}\n".encode())
        self._fw.stdin.flush()

        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                # The buffer was cleared before sending, so any prompt now is
                # the one printed after this command completed.
                if PROMPT in self._buf:
                    break
            time.sleep(0.02)

        with self._lock:
            response = self._buf

        # Strip the shell's ANSI colouring so callers can regex plain text.
        return re.sub(r"\x1b\[[0-9;]*m", "", response)

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
        return False
