"""
Abstract base class for test instruments (power supplies, virtual injection, etc.)
"""

from abc import ABC, abstractmethod


class InstrumentBase(ABC):
    """Abstract base for stimulus instruments."""

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
