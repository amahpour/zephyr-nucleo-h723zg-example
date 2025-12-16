# ADC Sampler - Zephyr Application

[![Tests](https://github.com/USERNAME/zephyr-nucleo-h723zg-example/actions/workflows/tests.yml/badge.svg)](https://github.com/USERNAME/zephyr-nucleo-h723zg-example/actions/workflows/tests.yml)

Periodically samples ADC channels and exposes values via UART shell commands.

## Prerequisites

Install Zephyr RTOS. Quick minimal setup:

```bash
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west

west init ~/zephyrproject
cd ~/zephyrproject
west update zephyr hal_stm32
west zephyr-export
west packages pip --install

cd zephyr
west sdk install -t x86_64-zephyr-elf -t arm-zephyr-eabi
```

Or follow the full [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

## Build and Run

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example
west build -b qemu_x86 app --pristine
west build -t run
```

## Shell Commands

| Command | Description |
|---------|-------------|
| `adcregs` | Show ADC register values |
| `adcset <ch> <mv>` | Inject ADC value (simulator only) |
| `help` | List all commands |

Example:
```
uart:~$ adcregs
ADC Register File:
  seq:       5
  timestamp: 500 ms
  channels:
    ch[0]: 1650 mV
    ch[1]: 1650 mV

uart:~$ adcset 0 2500
Set ch[0] = 2500 mV
```

## QEMU Controls

| Key | Action |
|-----|--------|
| `Ctrl+A` then `X` | Exit QEMU |
| `Ctrl+A` then `C` | QEMU monitor |
| `Ctrl+A` then `H` | Help |

## More Documentation

See [docs/](docs/) for:
- [Architecture](docs/architecture.md)
- [Hardware Setup](docs/hardware.md)
- [Python Serial Testing](docs/serial-testing.md)

## License

SPDX-License-Identifier: Apache-2.0
