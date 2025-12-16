"""
Instrument abstraction layer for test stimulus (power supplies, virtual injection, etc.)
"""

from .base import InstrumentBase
from .virtual import VirtualInstrument

# RigolDP832Adapter is imported lazily to avoid pyvisa dependency for virtual tests
# Use: from instruments.rigol_adapter import RigolDP832Adapter

__all__ = ["InstrumentBase", "VirtualInstrument"]
