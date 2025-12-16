"""
Rigol VISA Module

This module contains classes and functions for interfacing with the following
Rigol Programmable Power Supplies:
- DP712
- DP821
- DP832

Adapted from: https://fixturfab.com/articles/controlling-rigol-dp832-using-pyvisa/
Copied from ~/code/rigol_dp832_mcp_server/rigol_dp832/rigol_dp.py
"""

import logging

from .comm_base import CommBase


class DP(CommBase):
    """
    Generic Rigol DPxxx Power Supply

    This class serves as the base for a controller of a Rigol DPxxx series
    programmable power supply. To create a controller class for a specific
    power supply, inherit from this class, and then implement the
    `channel_check` function to verify that the provided channel number is
    supported by the power supply.
    """

    def channel_check(self, channel):
        """
        Implemented by each power supply model to verify that the channel
        specified actually exists for the given model
        """
        assert NotImplementedError

    def get_output_mode(self, channel: int) -> str:
        """
        Query the current output mode of the specified channel

        DP800 series power supplies provide three output modes, including CV
        (constant voltage), CC (constant current), and UT (unregulated). In CV
        mode, the output voltage equals the voltage setting value and the
        output current is determined by the load. In CC mode, the output
        current equals the current setting value and the output voltage is
        determined by the load. UR mode is the critical mode between CC and CV
        modes.

        :param channel: Channel to get output state of, can be 1, 2, or 3.
        :type channel: int
        :return: Output mode string, CC, CV, or UR
        :rtype: str
        """
        self.channel_check(channel)
        return self.query_device(f":OUTP:MODE? CH{channel}").strip()

    def get_ocp_alarm(self, channel: int = "") -> bool:
        """
        Query whether OCP occurred on the specified channel.

        :param channel: Channel to get OCP alarm start of, can be 1, 2, or 3
        :type channel: int
        :return: Alarm state, True or False
        :rtype: bool
        """
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"

        alarm_state = self.query_device(f":OUTP:OCP:ALAR?{channel}").strip()
        return alarm_state == "YES"

    def clear_ocp_alarm(self, channel: int = ""):
        """Clear the OCP alarm on the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"

        self.inst.write(f":OUTP:OCP:CLEAR{channel}")

    def set_ocp_enabled(self, channel: int, state: bool):
        """Enable or disable overcurrent protection (OCP) of the specified channel."""
        self.channel_check(channel)

        if state:
            state = "ON"
        else:
            state = "OFF"

        self.inst.write(f":OUTP:OCP CH{channel},{state}")

    def get_ocp_enabled(self, channel: int = "") -> bool:
        """Query the status of OCP on the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"

        state = self.query_device(f":OUTP:OCP?{channel}").strip()
        logging.debug(f"state: {state}")
        return state == "ON"

    def set_ocp_value(self, channel: int, setting: float):
        """Set the OCP value of the specified channel."""
        self.channel_check(channel)
        self.inst.write(f":OUTP:OCP:VAL CH{channel},{setting}")

    def get_ocp_value(self, channel: int) -> float:
        """Query the OCP value of the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"
        return float(self.query_device(f":OUTP:OCP:VAL?{channel}"))

    def get_ovp_alarm(self, channel: int = "") -> bool:
        """Query whether OVP occurred on the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"
        state = self.query_device(f":OUTP:OVP:ALAR?{channel}").strip()
        return state == "ON"

    def clear_ovp_alarm(self, channel: int = ""):
        """Clear the OVP alarm on the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"

        self.inst.write(f":OUTP:OVP:CLEAR{channel}")

    def set_ovp_enabled(self, channel: int, state: bool):
        """Enable or disable OVP of the specified channel."""
        self.channel_check(channel)

        if state:
            state = "ON"
        else:
            state = "OFF"

        self.inst.write(f":OUTP:OVP CH{channel},{state}")

    def get_ovp_enabled(self, channel: int = "") -> bool:
        """Query the status of OVP on the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"

        state = self.query_device(f":OUTP:OVP?{channel}").strip()
        logging.debug(f"state: {state}")
        return state == "ON"

    def set_ovp_value(self, channel: int, setting: float):
        """Set the OVP value of the specified channel."""
        self.channel_check(channel)
        self.inst.write(f":OUTP:OVP:VAL CH{channel},{setting}")

    def get_ovp_value(self, channel: int) -> float:
        """Query the OVP value of the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"
        return float(self.query_device(f":OUTP:OVP:VAL?{channel}"))

    def set_output_state(self, channel: int, state: bool):
        """
        Enable or disable the output of the specified channel.

        :param channel: Channel to set enable state of
        :type channel: int
        :param state: True to enable, False to disable
        :type state: bool
        """
        if state:
            state = "ON"
        else:
            state = "OFF"

        self.inst.write(f":OUTP:STAT CH{channel},{state}")

    def get_output_state(self, channel: int = "") -> bool:
        """Query the output status of the specified channel."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"

        state = self.query_device(f":OUTP:STAT?{channel}").strip()
        return state == "ON"

    def set_channel_settings(self, channel, voltage, current):
        """
        Set voltage and current settings of the specified channel.

        :param channel: channel to set settings of
        :type channel: int
        :param voltage: voltage to set (V)
        :type voltage: float
        :param current: current to set (A)
        :type current: float
        """
        self.channel_check(channel)
        self.inst.write(f":APPL CH{channel},{voltage},{current}")

    def get_channel_settings(self, channel: int = "") -> dict:
        """Query the specified channels current settings."""
        self.channel_check(channel)

        if isinstance(channel, int):
            channel = f" CH{channel}"
        settings = self.query_device(f":APPL?{channel}").strip().split(",")
        return {"voltage": float(settings[-2]), "current": float(settings[-1])}

    def measure_current(self, channel):
        """Get the current measurement for the given channel."""
        self.channel_check(channel)
        meas = self.query_device(f":MEAS:CURR? CH{channel}").strip()
        return float(meas)

    def measure_voltage(self, channel):
        """Get the voltage measurement for the given channel."""
        self.channel_check(channel)
        meas = self.query_device(f":MEAS? CH{channel}").strip()
        return float(meas)

    def measure_all(self, channel):
        """Get the voltage, current, and power measurements for the channel."""
        self.channel_check(channel)
        meas = self.query_device(f":MEAS:ALL? CH{channel}").strip().split(",")
        return {
            "voltage": float(meas[0]),
            "current": float(meas[1]),
            "power": float(meas[2]),
        }


class DP712(DP):
    """Rigol DP712 Programmable Power Supply."""

    def channel_check(self, channel):
        assert channel in [1, ""], f"Output channel {channel} not supported"


class DP821(DP):
    """Rigol DP821 Programmable Power Supply."""

    def channel_check(self, channel):
        assert channel in [1, 2, ""], f"Output channel {channel} not supported"


class DP832(DP):
    """Rigol DP832 Programmable Power Supply."""

    def channel_check(self, channel):
        assert channel in [1, 2, 3, ""], f"Output channel {channel} not supported"
