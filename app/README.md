# ADC Sampler - Zephyr Application

A Zephyr-based ADC sampler with UART register access, supporting both QEMU simulation and hardware targets.

## Prerequisites

This application requires a Zephyr RTOS installation. Zephyr lives in its own
directory (the docs use `~/zephyrproject/` as an example) - this repo is a
**separate, standalone application** that references that installation.

### Quick Setup (Minimal Download)

By default, `west update` downloads every HAL for every supported chip - several GB.
For this project you only need the STM32 HAL:

```bash
# Create and activate virtual environment
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate

# Install west
pip install west

# Initialize Zephyr (just metadata, no big downloads yet)
west init ~/zephyrproject
cd ~/zephyrproject

# Fetch only what's needed:
#   - zephyr: core + x86/QEMU support (built-in)
#   - hal_stm32: for NUCLEO-H723ZG hardware
west update zephyr hal_stm32

# Register Zephyr location and install Python deps
west zephyr-export
west packages pip --install

# Install SDK (use -t for specific toolchains only)
cd zephyr
west sdk install -t x86_64-zephyr-elf -t arm-zephyr-eabi
```

This fetches ~500MB instead of several GB.

**In new terminals**, activate the venv before building:
```bash
source ~/zephyrproject/.venv/bin/activate
```

### Full Setup

Alternatively, follow the [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
for a complete installation with all HALs.

### Troubleshooting

**"C compiler not found"**: Ensure both toolchains are installed:
```bash
west sdk install -t x86_64-zephyr-elf -t arm-zephyr-eabi
```

**"unknown command 'build'"**: Set ZEPHYR_BASE so west can find the Zephyr extensions:
```bash
export ZEPHYR_BASE=~/zephyrproject/zephyr
```

## Building and Running

### Simulator (QEMU x86)

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example
west build -b qemu_x86 app --pristine
west build -t run
```

To exit QEMU: Press `Ctrl+A` then `X`

### Hardware (NUCLEO-H723ZG)

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example
west build -b nucleo_h723zg app --pristine
west flash
```

**Tip:** Add `export ZEPHYR_BASE=~/zephyrproject/zephyr` to your `~/.bashrc` to avoid typing it each time.

## Target Selection

The project uses Kconfig to select between simulator and hardware targets:

| Target | Board | Config Symbol |
|--------|-------|---------------|
| Simulator | `qemu_x86` | `CONFIG_APP_TARGET_SIM=y` |
| Hardware | `nucleo_h723zg` | `CONFIG_APP_TARGET_HW=y` |

Board-specific settings are in `boards/<board>.conf`.

## Architecture

```
src/                    # Shared, target-agnostic code
  main.c                # Application entry + sampling thread
  regs.h/c              # Thread-safe register file
  adc_backend.h         # ADC interface (no implementation)
  cmd_read_regs.c       # adcregs shell command

targets/
  sim/                  # Simulator-only (compiled when CONFIG_APP_TARGET_SIM)
    adc_backend.c       # ADC-emul driver + injection support
    cmd_inject_adc.c    # adcset shell command
  hw/                   # Hardware-only (compiled when CONFIG_APP_TARGET_HW)
    adc_backend.c       # Real ADC driver
```

The SIM-only code is **never compiled** into hardware builds.

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_APP_NUM_CH` | 4 | Number of ADC channels to sample |
| `CONFIG_APP_SAMPLE_PERIOD_MS` | 100 | Sampling interval in milliseconds |

## Shell Commands

Type `help` in the shell to see available commands.

### `adcregs` - Read ADC register file

Displays the current ADC register file contents:

```
uart:~$ adcregs
ADC Register File:
  seq:       42
  timestamp: 4200 ms
  channels:
    ch[0]: 1650 mV
    ch[1]: 825 mV
    ch[2]: 2475 mV
    ch[3]: 3300 mV
```

- **seq**: Sequence number (increments with each sample)
- **timestamp**: Time of last sample (milliseconds since boot)
- **ch[N]**: Channel N voltage in millivolts

### `adcset` - Inject ADC value (Simulator Only)

**This command only exists in simulator builds.** It sets the ADC emulator
to return a specific value for a channel.

```
uart:~$ adcset 0 2500
Set ch[0] = 2500 mV
Next sample will reflect this value.

uart:~$ adcregs
ADC Register File:
  seq:       43
  timestamp: 4300 ms
  channels:
    ch[0]: 2500 mV
    ch[1]: 1650 mV
    ch[2]: 1650 mV
    ch[3]: 1650 mV
```

On hardware builds, this command does not exist:

```
uart:~$ adcset 0 2500
adcset: command not found
```

## Sampling Behavior

The application runs a dedicated sampling thread that:

1. Calls `adc_backend_sample_all()` to read all channels
2. Updates the register file with new values
3. Sleeps for `CONFIG_APP_SAMPLE_PERIOD_MS` milliseconds
4. Repeats

The `seq` field increments with each successful sample, allowing you to verify sampling is active.

## ADC Emulator (Simulator Only)

The simulator uses Zephyr's `adc-emul` driver defined in `boards/qemu_x86.overlay`:

- 4 channels (configurable via `CONFIG_APP_NUM_CH`)
- 3.3V reference voltage
- 12-bit resolution
- Initial value: 1650 mV (mid-scale)

The emulator allows injecting specific values for testing via the `adcset` command.

## Hardware ADC Configuration (TODO)

When hardware arrives, configure the ADC in `targets/hw/adc_backend.c`:

1. Set `ADC_CONFIGURED=1`
2. Define the correct ADC node label (e.g., `adc1`)
3. Configure channel mappings for your wiring

**NUCLEO-H723ZG Arduino Header ADC Pins:**

| Arduino Pin | STM32 Pin | ADC Channel |
|-------------|-----------|-------------|
| A0 | PA3 | ADC1_INP15 |
| A1 | PC0 | ADC1_INP10 |
| A2 | PC3 | ADC1_INP13 |
| A3 | PB1 | ADC1_INP5 |
| A4 | PC2 | ADC1_INP12 |
| A5 | PF10 | ADC3_INP6 |

You may need a devicetree overlay (`boards/nucleo_h723zg.overlay`) to enable
specific ADC channels.

## Example Session (Simulator)

```
*** Booting Zephyr OS build v4.3.0-2442-gd5982f0f89f8 ***
I: ADC Sampler application started
I: ADC backend (SIM) initialized with 4 channels
ADC Sampler ready. Type 'help' for available commands.
I: Sampling thread started (period=100 ms)

uart:~$ adcregs
ADC Register File:
  seq:       5
  timestamp: 500 ms
  channels:
    ch[0]: 1650 mV
    ch[1]: 1650 mV
    ch[2]: 1650 mV
    ch[3]: 1650 mV

uart:~$ adcset 0 2500
Set ch[0] = 2500 mV
Next sample will reflect this value.

uart:~$ adcregs
ADC Register File:
  seq:       6
  timestamp: 600 ms
  channels:
    ch[0]: 2500 mV
    ch[1]: 1650 mV
    ch[2]: 1650 mV
    ch[3]: 1650 mV

uart:~$ adcset 1 0
Set ch[1] = 0 mV
Next sample will reflect this value.

uart:~$ adcset 2 3300
Set ch[2] = 3300 mV
Next sample will reflect this value.

uart:~$ adcregs
ADC Register File:
  seq:       8
  timestamp: 800 ms
  channels:
    ch[0]: 2500 mV
    ch[1]: 0 mV
    ch[2]: 3300 mV
    ch[3]: 1650 mV
```

## Programmatic Serial Access (Python)

You can interact with the shell programmatically using pyserial - same code works
for both QEMU simulation and real hardware.

### Running QEMU with PTY serial

```bash
# Terminal 1: Build first (if not already built)
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr
cd ~/code/zephyr-nucleo-h723zg-example
west build -b qemu_x86 app

# Run QEMU directly with PTY serial (adjust SDK path as needed)
cd build
~/zephyr-sdk-0.17.4/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-system-i386 \
  -m 32 -cpu qemu32,+nx,+pae -machine q35 \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -no-reboot -nographic -machine acpi=off -net none \
  -serial pty \
  -kernel zephyr/zephyr.elf

# QEMU will print:
# char device redirected to /dev/pts/X (label serial0)
```

### Connect with Python

```bash
# Terminal 2: Run test script
pip install pyserial
python3 scripts/test_serial.py /dev/pts/3
```

Or use pyserial directly:

```python
import serial

ser = serial.Serial('/dev/pts/3', baudrate=115200, timeout=1)
ser.write(b'adcregs\r\n')
print(ser.read(4096).decode())
```

### For real hardware

Same code, just change the port:

```python
ser = serial.Serial('/dev/ttyACM0', baudrate=115200, timeout=1)
```

## Test Checklist

- [ ] **QEMU Happy Path**
  - Boot and run `adcregs` - see initial values
  - Run `adcset 1 2500` - set channel 1
  - Wait 100ms, run `adcregs` - see updated value

- [ ] **Cadence Verification**
  - Run `adcregs` multiple times
  - Verify `seq` increments at ~10 Hz (100ms period)
  - Verify timestamps update accordingly

- [ ] **HW Separation**
  - Build for hardware: `west build -b nucleo_h723zg app --pristine`
  - Build succeeds without SIM code
  - (Optional) `strings build/zephyr/zephyr.elf | grep adcset` returns empty

## Current Status

- [x] Basic shell enabled
- [x] Target selection (SIM/HW)
- [x] Register file with mutex protection
- [x] adcregs command
- [x] ADC sampling thread
- [x] ADC emulator integration (SIM)
- [x] adcset command (SIM only)
- [x] Hardware ADC skeleton with TODOs

## License

SPDX-License-Identifier: Apache-2.0

