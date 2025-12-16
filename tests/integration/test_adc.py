#!/usr/bin/env python3
"""
Generic ADC integration tests.

These tests work with any DUT/instrument combination defined in the config file.
Tests use abstract interfaces so they can run against:
- Virtual: QEMU + VirtualInstrument (adcset injection)
- Physical: Real hardware + Rigol DP832 power supply
"""

import re
import time

import pytest


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

        # Should have all 4 channels
        for i in range(4):
            assert f"ch[{i}]:" in response, f"Expected channel {i} data"

    def test_inject_and_read_single_channel(self, dut, instrument):
        """Test injecting a single ADC value and reading it back."""
        test_voltage = 2500  # mV

        # Enable output and set voltage
        instrument.enable_output(0, True)
        instrument.set_voltage(0, test_voltage)

        # Wait for ADC sampling
        time.sleep(0.2)

        # Read back and verify
        response = dut.send_command("adcregs")
        actual_voltage = parse_channel_value(response, 0)

        # Allow for ADC quantization error (within 50mV for physical, 10mV for virtual)
        tolerance = 50
        assert abs(actual_voltage - test_voltage) <= tolerance, (
            f"ch[0]={actual_voltage}mV, expected ~{test_voltage}mV (tolerance={tolerance})"
        )

    def test_inject_and_read_multiple_channels(self, dut, instrument):
        """Test injecting values on multiple channels and reading them back."""
        test_values = {
            0: 2500,
            1: 1000,
        }

        # Set voltages on multiple channels
        for channel, voltage in test_values.items():
            instrument.enable_output(channel, True)
            instrument.set_voltage(channel, voltage)

        # Wait for sampling
        time.sleep(0.2)

        # Read back and verify
        response = dut.send_command("adcregs")

        tolerance = 50
        for channel, expected_voltage in test_values.items():
            actual_voltage = parse_channel_value(response, channel)
            assert abs(actual_voltage - expected_voltage) <= tolerance, (
                f"ch[{channel}]={actual_voltage}mV, expected ~{expected_voltage}mV"
            )

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

    def test_voltage_range_low(self, dut, instrument):
        """Test low voltage injection."""
        test_voltage = 100  # mV

        instrument.enable_output(0, True)
        instrument.set_voltage(0, test_voltage)
        time.sleep(0.2)

        response = dut.send_command("adcregs")
        actual_voltage = parse_channel_value(response, 0)

        tolerance = 50
        assert abs(actual_voltage - test_voltage) <= tolerance, (
            f"ch[0]={actual_voltage}mV, expected ~{test_voltage}mV"
        )

    def test_voltage_range_high(self, dut, instrument):
        """Test high voltage injection (near 3.3V reference)."""
        test_voltage = 3000  # mV

        instrument.enable_output(0, True)
        instrument.set_voltage(0, test_voltage)
        time.sleep(0.2)

        response = dut.send_command("adcregs")
        actual_voltage = parse_channel_value(response, 0)

        tolerance = 50
        assert abs(actual_voltage - test_voltage) <= tolerance, (
            f"ch[0]={actual_voltage}mV, expected ~{test_voltage}mV"
        )
