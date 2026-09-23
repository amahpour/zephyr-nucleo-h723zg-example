# ADC Sampler - Zephyr Application

[![Tests](https://github.com/amahpour/zephyr-nucleo-h723zg-example/actions/workflows/tests.yml/badge.svg)](https://github.com/amahpour/zephyr-nucleo-h723zg-example/actions/workflows/tests.yml)

Periodically samples ADC channels and exposes values via UART shell commands.

The same firmware and the same drivers run three ways: under QEMU, against a
**virtual PCB** whose chips are separate OS processes, and on a real
NUCLEO-H723ZG. The tests don't know which one they're talking to.

**[How the virtual PCB works →](https://amahpour.github.io/zephyr-nucleo-h723zg-example/)**
— an illustrated tour of the three processes, what crosses each boundary, and
what happens when a chip isn't there.

## Prerequisites

Install Zephyr RTOS. The Zephyr revision is pinned in [`ZEPHYR_REVISION`](ZEPHYR_REVISION)
and the commands below read it from that file, so local setup and CI cannot drift apart.
Run them from a checkout of this repository.

```bash
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west

west init -m https://github.com/zephyrproject-rtos/zephyr \
    --mr "$(cat ZEPHYR_REVISION)" ~/zephyrproject
cd ~/zephyrproject
west update zephyr hal_stm32 cmsis cmsis_6
west packages pip --install
west zephyr-export

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

### Virtual PCB (native_sim)

The DAC chips run as separate OS processes and the firmware reaches them over a
Unix socket, so `dacset`, the DACx578 driver and the I²C path all execute exactly
as they do on hardware. Nothing is mocked and no board is involved.

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example

# the board and chip models - plain host C, no Zephyr involved
cmake -S vpcb -B build-vpcb && cmake --build build-vpcb

# the firmware
west build -b native_sim app -d build-vpcb-fw --pristine

# board first (it owns the netlist), then the chips, then the firmware
./build-vpcb/vpcb-board   --sock /tmp/vpcb.sock --netlist vpcb/netlists/adc_loopback.txt &
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48 &
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x4c &
./build-vpcb-fw/zephyr/zephyr.exe
```

At the shell, `dacset 0 2000` then `adcregs` reads back 1998 mV — the 2 mV is
the DAC code and the 12-bit conversion, not an error. Omit the two `vpcb-dac7578`
lines to see what the driver does when a chip is absent.

Stop everything with `pkill -f 'vpcb-board|vpcb-dac7578'; rm -f /tmp/vpcb.sock`.

For a step-by-step version with the real output at every stage, including three
ways to break the rig on purpose, see
[walkthroughs/01-virtual-pcb.md](walkthroughs/01-virtual-pcb.md).

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

The hardware build is configured for 15 ADC channels and two DAC7578s on I²C1 by
default. See [Hardware Setup](docs/hardware.md) for the DAC loopback test rig and
its wiring.

## Shell Commands

| Command | Available on | Description |
|---------|--------------|-------------|
| `adcregs` | all targets | Show ADC register values |
| `dacset <ch> <mv>` | hardware, virtual PCB | Drive a DAC channel through the DACx578 driver |
| `adcset <ch> <mv>` | QEMU only | Inject a value straight into the emulated ADC. Never built for hardware. |
| `help` | all targets | List all commands |

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

Run the same tests against QEMU, the virtual PCB, or real hardware.

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

# Virtual PCB (native_sim + DAC models as separate processes):
pytest test_adc.py --config=configs/vpcb.yaml -v

# Physical hardware (Nucleo + DAC loopback rig):
pytest test_adc.py --config=configs/physical.yaml -v
```

See [Hardware Setup](docs/hardware.md) for the loopback rig wiring.

## Walkthroughs

Follow-along tutorials with copy-pasteable commands and real captured output:

- [Run the virtual PCB](walkthroughs/01-virtual-pcb.md) — build a board out of
  Linux processes, drive a DAC across a socket, read it back on the ADC, then
  unplug a chip mid-session and watch the driver report it. No hardware needed.

## More Documentation

- [**Virtual PCB Test Loop**](https://amahpour.github.io/zephyr-nucleo-h723zg-example/)
  — the project site, built from [`docs/vpcb-loop.html`](docs/vpcb-loop.html)
  and published on every push to `main` that touches `docs/`
- [Architecture](docs/architecture.md) — how the target layer picks a backend at build time
- [Hardware Setup](docs/hardware.md) — the DAC loopback rig and its wiring
- [Python Serial Testing](docs/serial-testing.md)

## License

SPDX-License-Identifier: Apache-2.0
