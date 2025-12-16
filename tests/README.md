# Tests

This directory contains both unit tests and integration tests for the ADC Sampler.

Tests run automatically on every push and pull request via [GitHub Actions](../.github/workflows/tests.yml).

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

Python-based integration tests using pytest. Tests automatically start/stop QEMU:

```bash
# Install dependencies
pip install pytest pyserial

# Build the app first
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr
cd ~/code/zephyr-nucleo-h723zg-example
west build -b qemu_x86 app

# Run integration tests
pytest tests/integration/ -v
```

### Test Structure

- `tests/integration/` - Python integration tests
  - `test_serial.py` - Serial communication tests (pytest class)
  - `conftest.py` - Pytest configuration
  - `pytest.ini` - Pytest settings

### Features

- **Automatic QEMU management**: Fixtures handle QEMU startup/teardown
- **PTY auto-detection**: Automatically finds the PTY path from QEMU output
- **Serial connection management**: Fixtures handle connection lifecycle

