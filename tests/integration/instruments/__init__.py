"""
Instrument abstraction layer for test stimulus.

VirtualInstrument injects values straight into QEMU's emulated ADC.
DacInstrument drives the board's own DACx578s, which are wired back into its
ADC -- on the virtual PCB and on the bench alike.
"""

from .base import InstrumentBase
from .dac import DacInstrument
from .virtual import VirtualInstrument

__all__ = ["InstrumentBase", "DacInstrument", "VirtualInstrument"]
