# Tests

This directory contains both unit tests and integration tests for the ADC Sampler.

## CI Pipeline

Tests run automatically on every push and pull request via [GitHub Actions](../.github/workflows/tests.yml).

The pipeline runs in a single job to minimize environment setup time:

| Stage | Description |
|-------|-------------|
| **Environment Setup** | Install Zephyr SDK, modules (`hal_stm32`, `cmsis`, `cmsis_6`) |
| **Unit Tests** | Run ztest via `west twister` on QEMU |
| **Integration Tests** | Build QEMU app, run pytest against live firmware |
| **Firmware Build** | Build `nucleo_h723zg` target to verify ARM compilation |

Artifacts uploaded: test results, firmware binaries (`.elf`, `.bin`, `.hex`)

## Unit Tests

Zephyr ztest-based unit tests that run on QEMU:

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example
west twister -T tests/unit -p qemu_x86
```

### Test Structure

- `tests/unit/` - Zephyr test app
  - `CMakeLists.txt` - Build configuration
  - `prj.conf` - Test configuration
  - `testcase.yaml` - Twister test metadata
  - `src/test_regs.c` - Register file unit tests

### Running Individual Tests

```bash
west twister -p qemu_x86 -s unit.regs
```

## Integration Tests

Python-based integration tests using pytest with a config-driven architecture.
The same tests run unmodified against three setups: QEMU, the virtual PCB, and
real hardware.

### Quick Start (Virtual/QEMU)

```bash
# Install dependencies
pip install -r tests/integration/requirements.txt

# Build the app first
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr
cd ~/code/zephyr-nucleo-h723zg-example
west build -b qemu_x86 app -d build-qemu --pristine

# Run integration tests
PYTHONPATH=tests/integration pytest tests/integration/ \
    --config=tests/integration/configs/virtual.yaml -v
```

### Test Structure

```
tests/integration/
├── configs/
│   ├── virtual.yaml        # QEMU + adcset injection
│   ├── vpcb.yaml           # native_sim + DAC models as separate processes
│   └── physical.yaml       # Real Nucleo + DAC loopback rig
├── devices/
│   ├── base.py             # DUTBase abstract interface
│   ├── qemu.py             # QEMU device (PTY auto-detection)
│   ├── native_sim.py       # native_sim + virtual PCB stack launcher
│   └── physical.py         # Physical UART device
├── instruments/
│   ├── base.py             # InstrumentBase abstract interface
│   ├── virtual.py          # adcset injection into QEMU's emulated ADC
│   └── dac.py              # dacset through the DUT's own DACs (vPCB and hardware)
├── conftest.py             # Config-driven pytest fixtures
├── pytest.ini              # Pytest settings
└── test_adc.py             # Generic ADC tests
```

### Features

- **Config-driven**: YAML files specify DUT and instrument types
- **Generic tests**: Same tests run against QEMU, the virtual PCB, or hardware
- **Layered abstraction**: `DUTBase` and `InstrumentBase` ABCs
- **Automatic QEMU management**: Fixtures handle QEMU startup/teardown
- **PTY auto-detection**: Automatically finds the PTY path from QEMU output

### Running Physical Tests

The physical rig is a DAC loopback: the Nucleo drives two DAC7578s on its own
I²C bus, wired back into its ADC. Wiring is in [docs/hardware.md](../docs/hardware.md).
No instrument libraries are needed — `physical.yaml` uses the same `dac`
instrument as the virtual PCB.

```bash
# Set the serial port in configs/physical.yaml (e.g. /dev/ttyACM0), then:
PYTHONPATH=tests/integration pytest tests/integration/ \
    --config=tests/integration/configs/physical.yaml -v
```
