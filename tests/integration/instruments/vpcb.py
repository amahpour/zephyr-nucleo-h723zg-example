"""
Virtual PCB instrument: the DAC itself.

The stimulus here is a real DACx578 register write that leaves the firmware
process, so unlike VirtualInstrument nothing is injected into the ADC. The
value reaches the ADC only by travelling the same path it takes on hardware.

Subclasses VirtualInstrument deliberately. test_adc.py branches on
`isinstance(instrument, VirtualInstrument)` to decide whether channels can be
reset in software between tests, and a virtual PCB can be -- unlike a bench
supply feeding a mux, where that reset costs real settling time.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from .virtual import VirtualInstrument

if TYPE_CHECKING:
    from ..devices.base import DUTBase


class VpcbInstrument(VirtualInstrument):
    """Drives channels by writing to DAC models running as separate processes."""

    def __init__(self, dut: DUTBase):
        super().__init__(dut)

    def set_voltage(self, channel: int, voltage_mv: int) -> None:
        """
        Drive a channel by writing the DAC over the virtual I2C bus.

        Args:
            channel: Board channel number (0-14)
            voltage_mv: Voltage in millivolts

        Raises:
            RuntimeError: if the bus write was not acknowledged
        """
        response = self._dut.send_command(f"dacset {channel} {voltage_mv}")

        if "OK" not in response:
            raise RuntimeError(
                f"DAC write failed for channel {channel} at {voltage_mv} mV. "
                f"A NAK here means no IC process is serving that address. "
                f"Response: {response!r}"
            )

    def enable_output(self, channel: int, enable: bool) -> None:
        """
        No-op: a DAC channel is always driving whatever its register holds.

        Powering a channel down is a real DACx578 command, but it is not the
        same operation as a supply output being switched off, so it is not
        conflated with one here.
        """
        pass
