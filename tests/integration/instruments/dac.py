"""
DAC instrument: the board drives its own test signal.

Voltages are set by sending `dacset` to the DUT, which writes a real DACx578
register through the firmware's own driver. The DAC output is wired back into
the board's ADC, so the value reaches the ADC by the same path whether the
DACs are processes on a virtual PCB or parts on the bench.

That is why this one class serves both `vpcb.yaml` and `physical.yaml`. It used
to be called VpcbInstrument and subclassed VirtualInstrument purely so that
test_adc.py would reset channels between tests. On the loopback rig real
hardware needs that reset too, so the question is now asked directly through
`can_reset_channels` rather than by pretending a DAC on copper is virtual.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from .base import InstrumentBase

if TYPE_CHECKING:
    from ..devices.base import DUTBase


class DacInstrument(InstrumentBase):
    """Drives channels through the DUT's DACx578s, virtual or real."""

    # A DAC settles in microseconds against a 100 ms sample period.
    can_reset_channels = True

    def __init__(self, dut: DUTBase):
        self._dut = dut

    def connect(self) -> None:
        """Nothing to connect: the DUT connection is the instrument."""

    def disconnect(self) -> None:
        """Nothing to disconnect: the DUT fixture owns the connection."""

    def set_voltage(self, channel: int, voltage_mv: int) -> None:
        """
        Drive a channel by writing its DAC.

        Args:
            channel: Board channel number (0-14)
            voltage_mv: Voltage in millivolts

        Raises:
            RuntimeError: if the write failed
        """
        response = self._dut.send_command(f"dacset {channel} {voltage_mv}")

        if "OK" not in response:
            raise RuntimeError(
                f"DAC write failed for channel {channel} at {voltage_mv} mV. "
                f"-19 means nothing acknowledged the address: on a virtual PCB "
                f"no IC process is serving it, on hardware the part is absent, "
                f"unpowered or mis-strapped. -5 means the virtual PCB itself "
                f"stopped answering. Response: {response!r}"
            )

    def enable_output(self, channel: int, enable: bool) -> None:
        """
        No-op: a DAC channel is always driving whatever its register holds.

        Powering a channel down is a real DACx578 command, but it is not the
        same operation as a supply output being switched off, so it is not
        conflated with one here.
        """
