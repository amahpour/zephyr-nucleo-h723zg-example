# Programmatic Serial Access (Python)

You can interact with the shell using pyserial - same code works for both
QEMU simulation and real hardware.

## Running QEMU with PTY Serial

```bash
# Build first
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr
cd ~/code/zephyr-nucleo-h723zg-example
west build -b qemu_x86 app

# Run QEMU with PTY serial
cd build
~/zephyr-sdk-0.17.4/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-system-i386 \
  -m 32 -cpu qemu32,+nx,+pae -machine q35 \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -no-reboot -nographic -machine acpi=off -net none \
  -serial pty \
  -kernel zephyr/zephyr.elf

# QEMU will print something like:
# char device redirected to /dev/pts/5 (label serial0)
# 
# Copy the /dev/pts/X path (e.g., /dev/pts/5) from this output.
```

## Using pytest (Recommended)

The integration tests use pytest with fixtures that automatically manage QEMU:

```bash
pip install pytest pyserial

# Build the app first
west build -b qemu_x86 app

# Run tests (QEMU starts/stops automatically)
pytest tests/integration/ -v
```

The pytest fixtures handle:
- Starting QEMU with PTY serial
- Detecting the PTY path automatically
- Opening/closing serial connections
- Cleaning up QEMU on test completion

## Direct pyserial Usage

```python
import serial

# For QEMU (replace /dev/pts/3 with the path QEMU printed)
ser = serial.Serial('/dev/pts/3', baudrate=115200, timeout=1)

# For real hardware
# ser = serial.Serial('/dev/ttyACM0', baudrate=115200, timeout=1)

# Send command
ser.write(b'adcregs\r\n')
print(ser.read(4096).decode())

# Inject value (simulator only)
ser.write(b'adcset 0 2500\r\n')
print(ser.read(4096).decode())
```

## Troubleshooting

**"C compiler not found"**: Install the toolchain:
```bash
west sdk install -t x86_64-zephyr-elf -t arm-zephyr-eabi
```

**"unknown command 'build'"**: Set ZEPHYR_BASE:
```bash
export ZEPHYR_BASE=~/zephyrproject/zephyr
```

