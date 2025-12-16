"""
Virtual instrument that injects ADC values via DUT shell commands.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from .base import InstrumentBase

if TYPE_CHECKING:
    from ..devices.base import DUTBase


class VirtualInstrument(InstrumentBase):
    """
    Virtual instrument that injects ADC values via DUT's adcset command.

    This is used for QEMU-based testing where we can inject values directly
    into the emulated ADC rather than using physical hardware.
    """

    def __init__(self, dut: DUTBase):
        """
        Initialize virtual instrument.

        Args:
            dut: DUT instance to send commands to
        """
        self._dut = dut

    def connect(self) -> None:
        """No-op for virtual instrument - uses DUT's connection."""
        pass

    def disconnect(self) -> None:
        """No-op for virtual instrument."""
        pass

    def set_voltage(self, channel: int, voltage_mv: int) -> None:
        """
        Inject ADC value via DUT's adcset command.

        Args:
            channel: ADC channel number (0-3)
            voltage_mv: Voltage in millivolts
        """
        response = self._dut.send_command(f"adcset {channel} {voltage_mv}")
        if f"Set ch[{channel}]" not in response:
            raise RuntimeError(f"Failed to inject ADC value: {response}")

    def enable_output(self, channel: int, enable: bool) -> None:
        """
        No-op for virtual instrument - virtual channels are always enabled.

        Args:
            channel: Channel number (ignored)
            enable: Enable state (ignored)
        """
        pass
