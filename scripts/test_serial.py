#!/usr/bin/env python3
"""
Test script for interacting with the ADC Sampler via serial (QEMU PTY or real UART).

Usage:
  # For QEMU: First start QEMU with PTY serial in another terminal:
  #   west build -t run -- -serial pty
  # QEMU will print: "char device redirected to /dev/pts/X"
  # Then run this script with that path:
  
  python3 scripts/test_serial.py /dev/pts/3
  
  # For real hardware:
  python3 scripts/test_serial.py /dev/ttyACM0
"""

import sys
import time
import serial


def send_command(ser: serial.Serial, cmd: str, timeout: float = 1.0) -> str:
    """Send a command and wait for the shell prompt to return."""
    # Clear any pending input
    ser.reset_input_buffer()
    
    # Send command
    ser.write(f"{cmd}\r\n".encode())
    
    # Read until we see the prompt again
    response = b""
    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting:
            response += ser.read(ser.in_waiting)
            # Check if we got the prompt back
            if b"uart:~$" in response:
                break
        time.sleep(0.05)
    
    return response.decode(errors='replace')


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 test_serial.py <serial_port>")
        print("  e.g., python3 test_serial.py /dev/pts/3")
        print("  e.g., python3 test_serial.py /dev/ttyACM0")
        sys.exit(1)
    
    port = sys.argv[1]
    
    print(f"Connecting to {port}...")
    ser = serial.Serial(port, baudrate=115200, timeout=1)
    time.sleep(0.5)  # Give it a moment to connect
    
    # Send empty line to get a fresh prompt
    send_command(ser, "")
    
    print("\n=== Test 1: Read initial ADC registers ===")
    response = send_command(ser, "adcregs")
    print(response)
    
    print("\n=== Test 2: Inject value on channel 0 ===")
    response = send_command(ser, "adcset 0 2500")
    print(response)
    
    print("\n=== Test 3: Inject value on channel 1 ===")
    response = send_command(ser, "adcset 1 1000")
    print(response)
    
    # Wait for sampling to occur
    print("\nWaiting 200ms for sampling...")
    time.sleep(0.2)
    
    print("\n=== Test 4: Read updated ADC registers ===")
    response = send_command(ser, "adcregs")
    print(response)
    
    # Verify the values (allow for ADC rounding - within 10mV)
    import re
    ch0_match = re.search(r'ch\[0\]:\s*(\d+)\s*mV', response)
    ch1_match = re.search(r'ch\[1\]:\s*(\d+)\s*mV', response)
    
    if ch0_match and ch1_match:
        ch0_val = int(ch0_match.group(1))
        ch1_val = int(ch1_match.group(1))
        
        ch0_ok = abs(ch0_val - 2500) <= 10
        ch1_ok = abs(ch1_val - 1000) <= 10
        
        if ch0_ok and ch1_ok:
            print(f"\n✓ SUCCESS: ch[0]={ch0_val}mV (expected ~2500), ch[1]={ch1_val}mV (expected ~1000)")
        else:
            print(f"\n✗ FAILED: ch[0]={ch0_val}mV, ch[1]={ch1_val}mV - values not as expected")
    else:
        print("\n✗ FAILED: Could not parse channel values from response")
    
    ser.close()
    print("\nDone.")


if __name__ == "__main__":
    main()

