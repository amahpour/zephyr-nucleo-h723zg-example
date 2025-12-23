#!/usr/bin/env python3
"""
Generic ADC integration tests.

These tests work with any DUT/instrument combination defined in the config file.
Tests use abstract interfaces so they can run against:
- Virtual: QEMU + VirtualInstrument (adcset injection)
- Physical: Real hardware + Rigol DP832 power supply + KB2040 mux
"""

import re
import time

import pytest

from instruments.virtual import VirtualInstrument


# Tolerance for physical hardware tests (in mV)
# ~100mV accounts for mux on-resistance (~70Ω) and ADC quantization
PHYSICAL_TOLERANCE = 150

# Default ADC value in virtual mode (0 mV)
VIRTUAL_DEFAULT_MV = 0


def parse_channel_value(response: str, channel: int) -> int:
    """
    Parse ADC channel value from adcregs response.

    Args:
        response: Response string from adcregs command
        channel: Channel number to parse

    Returns:
        Channel value in millivolts

    Raises:
        ValueError: If channel not found in response
    """
    pattern = rf"ch\[{channel}\]:\s*(\d+)\s*mV"
    match = re.search(pattern, response)
    if not match:
        raise ValueError(f"Could not find ch[{channel}] in response: {response}")
    return int(match.group(1))


def parse_sequence_number(response: str) -> int:
    """
    Parse sequence number from adcregs response.

    Args:
        response: Response string from adcregs command

    Returns:
        Sequence number

    Raises:
        ValueError: If sequence not found in response
    """
    match = re.search(r"seq:\s*(\d+)", response)
    if not match:
        raise ValueError(f"Could not find seq in response: {response}")
    return int(match.group(1))


class TestADC:
    """Generic ADC tests - work with any instrument/DUT combination."""

    def test_read_initial_registers(self, dut, num_test_channels):
        """Test reading ADC registers after boot."""
        response = dut.send_command("adcregs")

        # Verify basic structure
        assert "ADC Register File" in response, "Expected 'ADC Register File' header"
        assert "seq:" in response, "Expected sequence number"
        assert "timestamp:" in response, "Expected timestamp"
        assert "channels:" in response, "Expected channels section"

        # Should have channels 0 through num_test_channels-1
        for i in range(num_test_channels):
            assert f"ch[{i}]:" in response, f"Expected channel {i} data"

    def test_sequence_increment(self, dut):
        """Test that sequence number increments with sampling."""
        # Read initial sequence
        response1 = dut.send_command("adcregs")
        seq1 = parse_sequence_number(response1)

        # Wait for next sample (sampling period is ~100ms)
        time.sleep(0.15)

        # Read again
        response2 = dut.send_command("adcregs")
        seq2 = parse_sequence_number(response2)

        # Sequence should have incremented
        assert seq2 > seq1, f"seq should increment: {seq1} -> {seq2}"


class TestADCSingleChannel:
    """Test individual ADC channel accuracy."""

    def test_channel_accuracy_2v(self, dut, instrument, channel):
        """Test each channel reads ~2V accurately when driven."""
        test_voltage = 2000  # mV

        # Enable output and set voltage on the specified channel
        instrument.enable_output(channel, True)
        instrument.set_voltage(channel, test_voltage)

        # Wait for ADC sampling and signal to settle
        time.sleep(1.0)

        # Read back and verify
        response = dut.send_command("adcregs")
        actual_voltage = parse_channel_value(response, channel)

        assert abs(actual_voltage - test_voltage) <= PHYSICAL_TOLERANCE, (
            f"ch[{channel}]={actual_voltage}mV, expected ~{test_voltage}mV "
            f"(tolerance={PHYSICAL_TOLERANCE})"
        )

        # Reset channel to default for cleanup (virtual mode needs explicit reset)
        instrument.enable_output(channel, False)
        # For virtual instruments, reset to default value
        if isinstance(instrument, VirtualInstrument):
            instrument.set_voltage(channel, VIRTUAL_DEFAULT_MV)


class TestADCVoltageRange:
    """Test ADC voltage range on channel 0."""

    @pytest.mark.parametrize("test_voltage", [500, 1000, 1500, 2000, 2500, 3000])
    def test_voltage_sweep(self, dut, instrument, test_voltage):
        """Test various voltages on channel 0."""
        channel = 0

        instrument.enable_output(channel, True)
        instrument.set_voltage(channel, test_voltage)
        time.sleep(1.0)

        response = dut.send_command("adcregs")
        actual_voltage = parse_channel_value(response, channel)

        assert abs(actual_voltage - test_voltage) <= PHYSICAL_TOLERANCE, (
            f"ch[{channel}]={actual_voltage}mV, expected ~{test_voltage}mV"
        )

        # Reset channel to default for cleanup (virtual mode needs explicit reset)
        instrument.enable_output(channel, False)
        # For virtual instruments, reset to default value
        if isinstance(instrument, VirtualInstrument):
            instrument.set_voltage(channel, VIRTUAL_DEFAULT_MV)


class TestADCIsolation:
    """Test that ADC channels are properly isolated."""

    def test_channel_isolation(self, dut, instrument, driven_channel, num_test_channels):
        """Test that driving one channel doesn't affect others."""
        test_voltage = 2000  # mV

        # Reset all channels to default first (important for virtual mode)
        # This ensures previous tests don't affect this one
        if isinstance(instrument, VirtualInstrument):
            for ch in range(num_test_channels):
                instrument.set_voltage(ch, VIRTUAL_DEFAULT_MV)
            time.sleep(0.5)  # Wait for reset to take effect

        # Drive only the specified channel
        instrument.enable_output(driven_channel, True)
        instrument.set_voltage(driven_channel, test_voltage)
        time.sleep(1.0)

        response = dut.send_command("adcregs")

        # The driven channel should be at ~2V
        driven_value = parse_channel_value(response, driven_channel)
        assert abs(driven_value - test_voltage) <= PHYSICAL_TOLERANCE, (
            f"Driven ch[{driven_channel}]={driven_value}mV, expected ~{test_voltage}mV"
        )

        # Other channels should NOT be at 2V (they're floating)
        for ch in range(num_test_channels):
            if ch != driven_channel:
                other_value = parse_channel_value(response, ch)
                # Floating channels should not read close to the driven voltage
                # (allow wide range since floating, but not the driven voltage)
                is_isolated = abs(other_value - test_voltage) > 300
                if not is_isolated:
                    pytest.fail(
                        f"Channel isolation failed: ch[{ch}]={other_value}mV "
                        f"while ch[{driven_channel}] driven at {test_voltage}mV"
                    )

        # Reset channel to default for cleanup
        instrument.enable_output(driven_channel, False)
        # For virtual instruments, reset to default value
        if isinstance(instrument, VirtualInstrument):
            instrument.set_voltage(driven_channel, VIRTUAL_DEFAULT_MV)
