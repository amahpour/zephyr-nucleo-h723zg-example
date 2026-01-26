# ADC Sampler - Zephyr Application

[![Tests](https://github.com/amahpour/zephyr-nucleo-h723zg-example/actions/workflows/tests.yml/badge.svg)](https://github.com/amahpour/zephyr-nucleo-h723zg-example/actions/workflows/tests.yml)

Periodically samples ADC channels and exposes values via UART shell commands.

## Prerequisites

Install Zephyr RTOS. Quick minimal setup:

```bash
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west

west init ~/zephyrproject
cd ~/zephyrproject
west update zephyr hal_stm32 cmsis cmsis_6
west zephyr-export
west packages pip --install

cd zephyr
west sdk install -t x86_64-zephyr-elf arm-zephyr-eabi
```

Or follow the full [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

## Build and Run

### QEMU (Simulator)

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example
west build -b qemu_x86 app --pristine
west build -t run
```

### Physical Hardware (NUCLEO-H723ZG)

**Prerequisites:**
- Install OpenOCD: `sudo apt install openocd`
- Set up USB permissions (see [Hardware Setup](docs/hardware.md))
- Connect the board via USB

**Build and Flash:**

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example
west build -b nucleo_h723zg app --pristine
west flash --runner openocd
```

**Connect via Serial:**

```bash
# Find the serial port
# Linux: ls /dev/ttyACM* or ls /dev/ttyUSB*
# macOS: ls /dev/cu.usbmodem*
# Windows: Check Device Manager for COM port

# Connect (replace with your actual port)
screen /dev/ttyACM0 115200
# or
minicom -D /dev/ttyACM0 -b 115200
```

**Tip:** If you have multiple USB devices, identify ports by VID/PID using `lsusb` and `udevadm` (Linux) or `ioreg` (macOS). See [Hardware Setup](docs/hardware.md#identifying-device-ports-by-vidpid) for details.

For 15-channel ADC configuration with mux:
```bash
west build -b nucleo_h723zg app --pristine -- -DCONFIG_APP_NUM_CH=15
west flash --runner openocd
```

See [Hardware Setup](docs/hardware.md) for complete hardware configuration and wiring guide.

## Shell Commands

| Command | Description |
|---------|-------------|
| `adcregs` | Show ADC register values |
| `adcset <ch> <mv>` | Inject ADC value (QEMU simulator only, not available on hardware) |
| `help` | List all commands |

Example:
```
uart:~$ adcregs
ADC Register File:
  seq:       5
  timestamp: 500 ms
  channels:
    ch[0]: 0 mV
    ch[1]: 0 mV

uart:~$ adcset 0 2500
Set ch[0] = 2500 mV
```

## QEMU Controls

| Key | Action |
|-----|--------|
| `Ctrl+A` then `X` | Exit QEMU |
| `Ctrl+A` then `C` | QEMU monitor |
| `Ctrl+A` then `H` | Help |

## Integration Tests

Run automated tests against virtual (QEMU) or physical hardware.

### Setup

```bash
cd tests/integration
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### Run Tests

```bash
# Virtual (QEMU) - start QEMU first, then:
pytest test_adc.py --config=configs/virtual.yaml -v

# Physical hardware (Nucleo + Rigol DP832 + KB2040 mux):
pytest test_adc.py --config=configs/physical.yaml -v
```

See [Hardware Setup](docs/hardware.md) for physical test wiring guide.

## More Documentation

See [docs/](docs/) for:
- [Architecture](docs/architecture.md)
- [Hardware Setup](docs/hardware.md)
- [Python Serial Testing](docs/serial-testing.md)

## License

SPDX-License-Identifier: Apache-2.0
