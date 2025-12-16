"""
Abstract base class for Device Under Test (DUT) connections.
"""

from abc import ABC, abstractmethod


class DUTBase(ABC):
    """Abstract base for Device Under Test connection."""

    @abstractmethod
    def start(self) -> None:
        """Start/connect to the DUT."""
        pass

    @abstractmethod
    def stop(self) -> None:
        """Stop/disconnect from the DUT."""
        pass

    @abstractmethod
    def send_command(self, cmd: str, timeout: float = 1.0) -> str:
        """
        Send shell command and return response.

        Args:
            cmd: Command string to send
            timeout: Timeout in seconds to wait for response

        Returns:
            Response string from the DUT
        """
        pass
