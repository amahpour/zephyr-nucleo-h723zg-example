"""
Abstract base class for test instruments (power supplies, virtual injection, etc.)
"""

from abc import ABC, abstractmethod


class InstrumentBase(ABC):
    """Abstract base for stimulus instruments."""

    #: Whether a channel can be driven back to 0 V between tests at negligible
    #: cost. Tests use this to isolate themselves from whatever the previous
    #: test left driven. It is a property of the stimulus, not of whether the
    #: target is virtual: a DAC on real copper settles in microseconds and
    #: qualifies, while a bench supply behind an analog mux did not.
    can_reset_channels: bool = False

    @abstractmethod
    def connect(self) -> None:
        """Establish connection to instrument."""
        pass

    @abstractmethod
    def disconnect(self) -> None:
        """Close connection to instrument."""
        pass

    @abstractmethod
    def set_voltage(self, channel: int, voltage_mv: int) -> None:
        """
        Set output voltage on a channel.

        Args:
            channel: ADC channel number (0-14 for hardware, configurable for virtual)
            voltage_mv: Voltage in millivolts
        """
        pass

    @abstractmethod
    def enable_output(self, channel: int, enable: bool) -> None:
        """
        Enable or disable channel output.

        Args:
            channel: Channel number
            enable: True to enable, False to disable
        """
        pass
