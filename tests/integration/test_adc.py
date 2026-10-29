#!/usr/bin/env python3
"""
Generic ADC integration tests.

These tests work with any DUT/instrument combination defined in the config file.
Tests use abstract interfaces so they can run against:
- Virtual: QEMU + VirtualInstrument (adcset injection)
- Virtual PCB: native_sim + DacInstrument (DAC models as separate processes)
- Physical: Real hardware + DacInstrument (DACs on the board's own I2C bus)
"""

import math
import re
import time
from pathlib import Path

from sweep_plot import worst_errors, write_sweep


# How far a reading may sit from the voltage the DAC was set to, in mV.
#
# A DAC7578 at its datasheet limits is off by up to ~13 mV (offset, gain,
# quantisation - see DATASHEET_TOL_MV in test_faults.py). On the bench, every
# channel reads within 12 mV across the whole range, except A5, which carries
# about 30 mV of noise around a correct mean. 50 covers both, and still fails
# the ADC sampling-time bug, which read 214 mV low.
#
# It was 150, for a bench supply behind an analog mux that is no longer here.
PHYSICAL_TOLERANCE = 50

# The sweep steps every channel together across nearly the whole 3.3 V range
SWEEP_MV = range(50, 3251, 25)

# Where the sweep writes its readings and plot. Not tracked by git.
RESULTS_DIR = Path(__file__).parent / "results"

# Voltage channels are driven back to between tests, where the instrument
# can do that cheaply (see InstrumentBase.can_reset_channels)
RESET_MV = 0


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

def wait_for_fresh_sample(dut, min_advance: int = 2, timeout: float = 5.0,
                          poll_interval: float = 0.02) -> int:
    """
    Block until the firmware has completed a fresh sample cycle.

    Polls `adcregs` and returns once `seq` has advanced by `min_advance`. This
    replaces fixed sleeps: the firmware already publishes when it has resampled,
    so there is no reason to guess a duration.

    `min_advance` defaults to 2 rather than 1 because a single advance can be a
    cycle that was already in flight when the voltage was set, which does not
    prove the new value was sampled. Two guarantees a complete cycle after it.

    One code path serves every instrument. Settling is not modelled separately
    because nothing in the current rigs needs it: injection is instantaneous for
    the emulated ADC, and on hardware a DAC settles in microseconds against a
    100 ms sample period. Add a settle time when an instrument justifies one.

    Args:
        dut: Device under test
        min_advance: How far seq must advance before returning
        timeout: Seconds to wait before giving up
        poll_interval: Delay between polls, to avoid busy-waiting on the shell

    Returns:
        The sequence number observed after the advance

    Raises:
        TimeoutError: If seq does not advance far enough in time
    """
    start_seq = parse_sequence_number(dut.send_command("adcregs"))
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        seq = parse_sequence_number(dut.send_command("adcregs"))
        if seq - start_seq >= min_advance:
            return seq
        time.sleep(poll_interval)

    raise TimeoutError(
        f"seq did not advance by {min_advance} within {timeout}s "
        f"(stuck at {start_seq}). The firmware has stopped sampling."
    )


# The isolation test spreads its voltages across this range
ISOLATION_LOW_MV = 300
ISOLATION_HIGH_MV = 3000


def isolation_levels(num_channels: int, rotation: int) -> dict[int, int]:
    """
    The voltage every channel holds during one round of the isolation test.

    Each channel gets a different voltage, evenly spread from 300 mV to 3 V.
    Neighbouring channels are several steps apart, so two channels shorted
    together cannot both read their own value. Each round shifts the pattern
    by one step: after num_channels rounds, every channel has held every
    voltage once.

    Args:
        num_channels: Channels under test
        rotation: Which round, 0 to num_channels - 1

    Returns:
        {channel: millivolts}
    """
    n = num_channels
    step = (ISOLATION_HIGH_MV - ISOLATION_LOW_MV) / max(n - 1, 1)
    levels = [ISOLATION_LOW_MV + round(i * step) for i in range(n)]

    # The largest stride under n/2 that still visits every level once
    stride = next(s for s in range(max(n // 2, 1), 0, -1) if math.gcd(s, n) == 1)
    return {ch: levels[(ch * stride + rotation) % n] for ch in range(n)}


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

        # Wait for the firmware to report fresh samples. The real assertion is
        # the helper's timeout: if sampling has stopped, this raises.
        seq2 = wait_for_fresh_sample(dut)

        assert seq2 > seq1, f"seq should increment: {seq1} -> {seq2}"


class TestADCSingleChannel:
    """Test individual ADC channel accuracy."""

    def test_channel_accuracy_2v(self, dut, instrument, channel):
        """Test each channel reads ~2V accurately when driven."""
        test_voltage = 2000  # mV

        # Enable output and set voltage on the specified channel
        instrument.enable_output(channel, True)
        instrument.set_voltage(channel, test_voltage)

        # Wait for the ADC to actually resample rather than guessing a duration
        wait_for_fresh_sample(dut)

        # Read back and verify
        response = dut.send_command("adcregs")
        actual_voltage = parse_channel_value(response, channel)

        assert abs(actual_voltage - test_voltage) <= PHYSICAL_TOLERANCE, (
            f"ch[{channel}]={actual_voltage}mV, expected ~{test_voltage}mV "
            f"(tolerance={PHYSICAL_TOLERANCE})"
        )

        # Leave the channel as we found it, so the next test starts clean
        instrument.enable_output(channel, False)
        if instrument.can_reset_channels:
            instrument.set_voltage(channel, RESET_MV)


class TestADCVoltageRange:
    """Every channel, across the whole range."""

    def test_voltage_sweep(self, dut, instrument, num_test_channels, request):
        """
        Step every channel together from 50 mV to 3.25 V, 25 mV at a time, and
        check every reading.

        The readings are written to results/ as CSV, with their errors as an
        SVG plot: a pass or fail says whether there is a problem, the plot shows
        its shape.
        """
        channels = range(num_test_channels)
        rows = []
        for set_mv in SWEEP_MV:
            for ch in channels:
                instrument.enable_output(ch, True)
                instrument.set_voltage(ch, set_mv)
            wait_for_fresh_sample(dut)
            response = dut.send_command("adcregs")
            rows.append((set_mv, {ch: parse_channel_value(response, ch) for ch in channels}))

        # Leave every channel as we found it, so the next test starts clean
        for ch in channels:
            instrument.enable_output(ch, False)
            if instrument.can_reset_channels:
                instrument.set_voltage(ch, RESET_MV)

        config = Path(request.config.getoption("--config")).stem
        csv_path, svg_path = write_sweep(
            rows, RESULTS_DIR / f"voltage_sweep-{config}", PHYSICAL_TOLERANCE, config)

        worst = worst_errors(rows)
        print(f"\nWorst error per channel over {len(rows)} steps "
              f"(tolerance {PHYSICAL_TOLERANCE} mV):")
        for ch, (set_mv, err) in worst.items():
            print(f"  ch[{ch:2}] {err:+5d} mV at {set_mv} mV")
        print(f"Plot: {svg_path}")

        bad = [f"ch[{ch}] {err:+d} mV at {set_mv} mV"
               for ch, (set_mv, err) in worst.items() if abs(err) > PHYSICAL_TOLERANCE]
        assert not bad, (
            f"out of tolerance ({PHYSICAL_TOLERANCE} mV): " + "; ".join(bad)
            + f". Plot: {svg_path}"
        )


class TestADCIsolation:
    """Every channel held at its own voltage, all at the same time."""

    def test_channel_isolation(self, dut, instrument, rotation, num_test_channels):
        """
        Drive every channel to a different voltage at once and check each one
        reads its own. A channel that reads near a neighbour's voltage is
        shorted or crosstalking to it; one that reads off on its own is wrong.
        """
        expected = isolation_levels(num_test_channels, rotation)

        for ch, mv in expected.items():
            instrument.enable_output(ch, True)
            instrument.set_voltage(ch, mv)
        wait_for_fresh_sample(dut)

        response = dut.send_command("adcregs")
        wrong = []
        for ch, mv in expected.items():
            actual = parse_channel_value(response, ch)
            if abs(actual - mv) > PHYSICAL_TOLERANCE:
                wrong.append(f"ch[{ch}]={actual}mV, expected ~{mv}mV")

        # Leave every channel as we found it, so the next test starts clean
        for ch in expected:
            instrument.enable_output(ch, False)
            if instrument.can_reset_channels:
                instrument.set_voltage(ch, RESET_MV)

        assert not wrong, (
            f"rotation {rotation}: " + "; ".join(wrong)
            + f" (tolerance={PHYSICAL_TOLERANCE})"
        )
