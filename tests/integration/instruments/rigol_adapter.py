"""
Rigol DP832 adapter implementing InstrumentBase interface.
"""

from .base import InstrumentBase
from rigol_dp832.rigol_dp import DP832


class RigolDP832Adapter(InstrumentBase):
    """
    Adapter wrapping Rigol DP832 to match InstrumentBase interface.

    Maps ADC channels (0-3) to power supply channels (1-3) and converts
    voltage from millivolts to volts.
    """

    def __init__(
        self,
        visa_resource: str,
        channel: int = 1,
        current_limit: float = 0.1,
    ):
        """
        Initialize Rigol DP832 adapter.

        Args:
            visa_resource: VISA resource string (e.g., "TCPIP0::192.168.1.100::5555::SOCKET")
            channel: Power supply channel to use (1-3). This maps ADC channel to PS channel.
            current_limit: Current limit in amps (default 100mA for safety)
        """
        self._visa_resource = visa_resource
        self._ps_channel = channel  # Power supply channel (1-3)
        self._current_limit = current_limit
        self._dp832: DP832 = None

    def connect(self) -> None:
        """Connect to the Rigol DP832 power supply."""
        self._dp832 = DP832(
            conn_type="VISA",
            visa_resource_string=self._visa_resource,
        )

    def disconnect(self) -> None:
        """Disconnect from the power supply, disabling output first."""
        if self._dp832:
            try:
                # Disable output before disconnecting
                self._dp832.set_output_state(self._ps_channel, False)
            except Exception:
                pass
            try:
                self._dp832.close()
            except Exception:
                pass
            self._dp832 = None

    def set_voltage(self, channel: int, voltage_mv: int) -> None:
        """
        Set output voltage.

        Note: The channel parameter from the test (ADC channel 0-3) is ignored
        because the power supply channel is configured at init time. This allows
        flexibility in how the physical wiring maps ADC inputs to PS outputs.

        Args:
            channel: ADC channel (0-3) - currently ignored, uses configured PS channel
            voltage_mv: Voltage in millivolts
        """
        if not self._dp832:
            raise RuntimeError("Not connected. Call connect() first.")

        voltage_v = voltage_mv / 1000.0
        self._dp832.set_channel_settings(
            self._ps_channel,
            voltage_v,
            self._current_limit,
        )

    def enable_output(self, channel: int, enable: bool) -> None:
        """
        Enable or disable power supply output.

        Args:
            channel: ADC channel (0-3) - currently ignored, uses configured PS channel
            enable: True to enable, False to disable
        """
        if not self._dp832:
            raise RuntimeError("Not connected. Call connect() first.")

        self._dp832.set_output_state(self._ps_channel, enable)
