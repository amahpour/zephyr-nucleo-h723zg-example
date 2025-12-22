"""
Instrument abstraction layer for test stimulus (power supplies, virtual injection, etc.)
"""

from .base import InstrumentBase
from .virtual import VirtualInstrument

# PhysicalInstrument is imported lazily to avoid equipment dependencies for virtual tests
# Use: from instruments.physical import PhysicalInstrument

__all__ = ["InstrumentBase", "VirtualInstrument"]
