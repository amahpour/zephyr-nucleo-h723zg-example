"""
Pytest configuration for integration tests.

This module provides config-driven fixtures that create the appropriate
DUT and instrument instances based on YAML configuration files.
"""

import pytest
import yaml
from pathlib import Path

from devices.qemu import QEMUDevice
from instruments.virtual import VirtualInstrument


# Mark all tests in this directory as integration tests
pytestmark = pytest.mark.integration


def pytest_addoption(parser):
    """Add custom command line options."""
    parser.addoption(
        "--config",
        action="store",
        required=True,
        help="Path to test configuration YAML file",
    )


@pytest.fixture(scope="session")
def test_config(request):
    """Load test configuration from YAML file."""
    config_path = request.config.getoption("--config")
    config_file = Path(config_path)

    if not config_file.exists():
        pytest.fail(f"Config file not found: {config_path}")

    with open(config_file) as f:
        return yaml.safe_load(f)


def create_dut(dut_config: dict):
    """Factory function to create DUT instance based on config."""
    dut_type = dut_config.get("type")

    if dut_type == "qemu":
        return QEMUDevice(
            kernel_path=dut_config.get("kernel_path"),
            boot_timeout=dut_config.get("boot_timeout", 5.0),
            zephyr_sdk_path=dut_config.get("zephyr_sdk_path"),
        )
    elif dut_type == "physical":
        # Import here to avoid dependency when not needed
        from devices.physical import PhysicalDevice

        return PhysicalDevice(
            port=dut_config.get("port"),
            baudrate=dut_config.get("baudrate", 115200),
        )
    else:
        raise ValueError(f"Unknown DUT type: {dut_type}")


def create_instrument(instrument_config: dict, dut):
    """Factory function to create instrument instance based on config."""
    inst_type = instrument_config.get("type")

    if inst_type == "virtual":
        return VirtualInstrument(dut)
    elif inst_type == "physical":
        # Import here to avoid dependency when not needed
        from instruments.physical import PhysicalInstrument

        psu_config = instrument_config.get("power_supply", {})
        mux_config = instrument_config.get("mux", {})

        return PhysicalInstrument(
            psu_config=psu_config,
            mux_config=mux_config,
        )
    else:
        raise ValueError(f"Unknown instrument type: {inst_type}")


@pytest.fixture(scope="class")
def dut(test_config):
    """Create and manage DUT lifecycle."""
    dut_config = test_config.get("dut", {})
    device = create_dut(dut_config)

    device.start()
    yield device
    device.stop()


@pytest.fixture(scope="class")
def instrument(test_config, dut):
    """Create and manage instrument lifecycle."""
    instrument_config = test_config.get("instrument", {})
    inst = create_instrument(instrument_config, dut)

    inst.connect()
    yield inst
    inst.disconnect()


@pytest.fixture(scope="session")
def num_test_channels(test_config):
    """Get the number of test channels from config."""
    return test_config.get("num_channels", 15)


def pytest_generate_tests(metafunc):
    """Dynamically parametrize tests based on config."""
    # Get config from request
    config_path = metafunc.config.getoption("--config")
    if config_path:
        config_file = Path(config_path)
        if config_file.exists():
            with open(config_file) as f:
                test_config = yaml.safe_load(f)
                num_channels = test_config.get("num_channels", 15)
                
                # Parametrize channel-based tests
                if "channel" in metafunc.fixturenames:
                    metafunc.parametrize("channel", range(num_channels))
                if "driven_channel" in metafunc.fixturenames:
                    metafunc.parametrize("driven_channel", range(num_channels))
