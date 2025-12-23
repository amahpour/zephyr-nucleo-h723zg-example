"""
Physical instrument that orchestrates power supply and mux for hardware testing.
"""

from .base import InstrumentBase
from rigol_dp832.rigol_dp import DP832


class PhysicalInstrument(InstrumentBase):
    """
    Physical instrument that orchestrates Rigol DP832 power supply and KB2040 mux.

    This instrument routes a single power supply output to multiple ADC channels
    via a multiplexer. When setting voltage on a channel, it:
    1. Selects the mux channel (routes PSU output to ADC input N)
    2. Sets the power supply voltage

    This allows testing all ADC channels with a single power supply output.
    """

    def __init__(
        self,
        psu_config: dict,
        mux_config: dict,
    ):
        """
        Initialize physical instrument.

        Args:
            psu_config: Power supply configuration dict with keys:
                - visa_resource: VISA resource string
                - channel: Power supply channel (1-3)
                - current_limit: Current limit in amps
            mux_config: Mux configuration dict with keys:
                - port: Serial port (e.g., "/dev/ttyACM1")
                - baudrate: Baud rate (default 115200)
        """
        self._psu_config = psu_config
        self._mux_config = mux_config
        self._psu: DP832 = None
        self._mux = None

    def connect(self) -> None:
        """Connect to power supply and mux."""
        # Import mux here to avoid dependency when not needed
        try:
            from mux_controller import MuxController
        except ImportError:
            raise ImportError(
                "mux_controller package not found. Install with: "
                "pip install git+https://github.com/amahpour/KB2040-CD74HCx4067-controller.git"
            )

        # Connect to power supply
        self._psu = DP832(
            conn_type="VISA",
            visa_resource_string=self._psu_config["visa_resource"],
        )

        # Connect to mux (connects automatically in __init__)
        port = self._mux_config.get("port")
        baudrate = self._mux_config.get("baudrate", 115200)
        timeout = self._mux_config.get("timeout", 2.0)
        self._mux = MuxController(port=port, baudrate=baudrate, timeout=timeout)

    def disconnect(self) -> None:
        """Disconnect from power supply and mux, disabling output first."""
        if self._psu:
            try:
                # Disable output before disconnecting
                ps_channel = self._psu_config.get("channel", 1)
                self._psu.set_output_state(ps_channel, False)
            except Exception:
                pass
            try:
                self._psu.close()
            except Exception:
                pass
            self._psu = None

        if self._mux:
            try:
                self._mux.close()
            except Exception:
                pass
            self._mux = None

    def set_voltage(self, channel: int, voltage_mv: int) -> None:
        """
        Set output voltage on a channel.

        This selects the mux channel first, then sets the power supply voltage.
        The same PSU output is routed to different ADC inputs via the mux.

        Args:
            channel: ADC channel number (0-15, depending on mux capacity)
            voltage_mv: Voltage in millivolts
        """
        if not self._psu or not self._mux:
            raise RuntimeError("Not connected. Call connect() first.")

        # Select mux channel (routes PSU output to ADC input N)
        self._mux.set_channel(channel)

        # Set power supply voltage
        ps_channel = self._psu_config.get("channel", 1)
        current_limit = self._psu_config.get("current_limit", 0.1)
        voltage_v = voltage_mv / 1000.0

        self._psu.set_channel_settings(
            ps_channel,
            voltage_v,
            current_limit,
        )

    def enable_output(self, channel: int, enable: bool) -> None:
        """
        Enable or disable power supply output on a channel.

        This selects the mux channel first, then enables/disables the PSU output.

        Args:
            channel: ADC channel number (0-15, depending on mux capacity)
            enable: True to enable, False to disable
        """
        if not self._psu or not self._mux:
            raise RuntimeError("Not connected. Call connect() first.")

        # Select mux channel
        self._mux.set_channel(channel)

        # Enable/disable power supply output
        ps_channel = self._psu_config.get("channel", 1)
        self._psu.set_output_state(ps_channel, enable)

