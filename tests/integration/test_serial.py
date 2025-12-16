#!/usr/bin/env python3
"""
Integration tests for ADC Sampler via serial (QEMU PTY or real UART).

Uses pytest with fixtures to handle QEMU startup/teardown.
"""

import pytest
import serial
import time
import re
import subprocess
import os
import signal
from pathlib import Path


class TestADCSerial:
    """Integration tests for ADC Sampler serial interface."""

    @pytest.fixture(scope="class")
    def qemu_process(self):
        """Start QEMU with PTY serial and return the process and PTY path."""
        # Find the QEMU binary
        zephyr_sdk = os.environ.get('ZEPHYR_SDK_INSTALL_DIR', 
                                    os.path.expanduser('~/zephyr-sdk-0.17.4'))
        qemu_bin = f"{zephyr_sdk}/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-system-i386"
        
        if not os.path.exists(qemu_bin):
            pytest.skip(f"QEMU binary not found at {qemu_bin}")
        
        # Find the kernel
        repo_root = Path(__file__).parent.parent.parent
        kernel = repo_root / "build" / "zephyr" / "zephyr.elf"
        
        if not kernel.exists():
            pytest.skip(f"Kernel not found at {kernel}. Build the app first.")
        
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
        
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )
        
        # Wait for QEMU to print the PTY path
        pty_path = None
        for _ in range(50):  # 5 second timeout
            line = process.stdout.readline()
            if not line:
                time.sleep(0.1)
                continue
            
            # Look for: char device redirected to /dev/pts/X
            match = re.search(r'/dev/pts/\d+', line)
            if match:
                pty_path = match.group(0)
                break
        
        if not pty_path:
            process.terminate()
            process.wait()
            pytest.fail("QEMU did not output PTY path")
        
        yield process, pty_path
        
        # Teardown: kill QEMU
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

    @pytest.fixture(scope="class")
    def serial_conn(self, qemu_process):
        """Open serial connection to QEMU."""
        process, pty_path = qemu_process
        
        # Wait a bit for QEMU to fully boot
        time.sleep(2)
        
        # Open serial connection
        ser = serial.Serial(pty_path, baudrate=115200, timeout=2)
        time.sleep(0.5)  # Give it a moment to connect
        
        # Clear any initial output and wait for prompt
        ser.reset_input_buffer()
        time.sleep(1)
        
        yield ser
        
        # Teardown: close serial
        ser.close()

    def send_command(self, ser, cmd, timeout=1.0):
        """Send a command and wait for the shell prompt to return."""
        ser.reset_input_buffer()
        ser.write(f"{cmd}\r\n".encode())
        
        response = b""
        start = time.time()
        while time.time() - start < timeout:
            if ser.in_waiting:
                response += ser.read(ser.in_waiting)
                if b"uart:~$" in response:
                    break
            time.sleep(0.05)
        
        return response.decode(errors='replace')

    def test_read_initial_registers(self, serial_conn):
        """Test reading ADC registers after boot."""
        response = self.send_command(serial_conn, "adcregs")
        
        assert "ADC Register File" in response
        assert "seq:" in response
        assert "timestamp:" in response
        assert "channels:" in response
        
        # Should have all channels
        for i in range(4):
            assert f"ch[{i}]:" in response

    def test_inject_and_read(self, serial_conn):
        """Test injecting ADC values and reading them back."""
        # Inject value on channel 0
        response = self.send_command(serial_conn, "adcset 0 2500")
        assert "Set ch[0] = 2500" in response
        
        # Inject value on channel 1
        response = self.send_command(serial_conn, "adcset 1 1000")
        assert "Set ch[1] = 1000" in response
        
        # Wait for sampling to occur
        time.sleep(0.2)
        
        # Read back and verify
        response = self.send_command(serial_conn, "adcregs")
        
        # Parse channel values (allow for ADC rounding - within 10mV)
        ch0_match = re.search(r'ch\[0\]:\s*(\d+)\s*mV', response)
        ch1_match = re.search(r'ch\[1\]:\s*(\d+)\s*mV', response)
        
        assert ch0_match, "Could not find ch[0] in response"
        assert ch1_match, "Could not find ch[1] in response"
        
        ch0_val = int(ch0_match.group(1))
        ch1_val = int(ch1_match.group(1))
        
        assert abs(ch0_val - 2500) <= 10, f"ch[0]={ch0_val}mV, expected ~2500"
        assert abs(ch1_val - 1000) <= 10, f"ch[1]={ch1_val}mV, expected ~1000"

    def test_sequence_increment(self, serial_conn):
        """Test that sequence number increments with sampling."""
        # Read initial seq
        response1 = self.send_command(serial_conn, "adcregs")
        seq1_match = re.search(r'seq:\s*(\d+)', response1)
        assert seq1_match, "Could not find seq in first read"
        seq1 = int(seq1_match.group(1))
        
        # Wait for next sample
        time.sleep(0.15)
        
        # Read again
        response2 = self.send_command(serial_conn, "adcregs")
        seq2_match = re.search(r'seq:\s*(\d+)', response2)
        assert seq2_match, "Could not find seq in second read"
        seq2 = int(seq2_match.group(1))
        
        # Sequence should have incremented
        assert seq2 > seq1, f"seq should increment: {seq1} -> {seq2}"
