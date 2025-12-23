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


# Number of ADC channels to test (A0-A5 for now)
NUM_TEST_CHANNELS = 6

# Tolerance for physical hardware tests (in mV)
# ~100mV accounts for mux on-resistance (~70Ω) and ADC quantization
PHYSICAL_TOLERANCE = 150


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

    def test_read_initial_registers(self, dut):
        """Test reading ADC registers after boot."""
        response = dut.send_command("adcregs")

        # Verify basic structure
        assert "ADC Register File" in response, "Expected 'ADC Register File' header"
        assert "seq:" in response, "Expected sequence number"
        assert "timestamp:" in response, "Expected timestamp"
        assert "channels:" in response, "Expected channels section"

        # Should have channels 0 through NUM_TEST_CHANNELS-1
        for i in range(NUM_TEST_CHANNELS):
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

    @pytest.mark.parametrize("channel", range(NUM_TEST_CHANNELS))
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

        # Disable output for cleanup
        instrument.enable_output(channel, False)

        assert abs(actual_voltage - test_voltage) <= PHYSICAL_TOLERANCE, (
            f"ch[{channel}]={actual_voltage}mV, expected ~{test_voltage}mV "
            f"(tolerance={PHYSICAL_TOLERANCE})"
        )


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

        instrument.enable_output(channel, False)

        assert abs(actual_voltage - test_voltage) <= PHYSICAL_TOLERANCE, (
            f"ch[{channel}]={actual_voltage}mV, expected ~{test_voltage}mV"
        )


class TestADCIsolation:
    """Test that ADC channels are properly isolated."""

    @pytest.mark.parametrize("driven_channel", range(NUM_TEST_CHANNELS))
    def test_channel_isolation(self, dut, instrument, driven_channel):
        """Test that driving one channel doesn't affect others."""
        test_voltage = 2000  # mV

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
        for ch in range(NUM_TEST_CHANNELS):
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

        instrument.enable_output(driven_channel, False)
