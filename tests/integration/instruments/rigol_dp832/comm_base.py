"""
Communication base class for Rigol instruments.

Copied from ~/code/rigol_dp832_mcp_server/rigol_dp832/comm_base.py
"""

import logging
from typing import Literal

import pyvisa

# Set up basic logging config
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    force=True,
)


class CommBase:
    # Define the valid connection types as a class-level attribute
    ConnType = Literal["VISA", "Socket"]

    def __init__(
        self,
        conn_type: ConnType,
        visa_resource_string: str = None,
    ):
        """
        Initialize the Rigol Programmable Power Supply

        :param conn_type: Connection type (VISA or Socket)
        :type conn_type: ConnType

        :param visa_resource_string: Complete VISA resource string (for direct connection)
        :type visa_resource_string: str
        """

        # Check for connection type and set up device accordingly
        self.visa_resource_string = visa_resource_string
        self.conn_type = conn_type

        if conn_type == "VISA":
            self.configure_visa(visa_resource_string)
        elif conn_type == "Socket":
            pass
        else:
            # Runtime check to enforce the allowed values
            raise ValueError(
                f"Invalid connection type: {conn_type}. Valid types are {self.ConnType.__args__}"
            )

    def configure_visa(self, visa_resource_string: str = None):
        self.rm = pyvisa.ResourceManager()

        # If a specific VISA resource string is provided, use it directly
        if visa_resource_string:
            logging.info(f"Using provided VISA resource: {visa_resource_string}")
            self.inst = self.rm.open_resource(visa_resource_string, read_termination="\n")
            return

        # If no resource string provided, raise an error
        raise ValueError("VISA resource string is required for VISA connection type")

    def query_device(self, command: str) -> str:
        """
        Wrapper function to query the device.

        :param command: Command to send to the device
        :type command: str
        :return: Response from the device
        :rtype: str
        """
        if self.conn_type == "VISA":
            return self.inst.query(command)
        elif self.conn_type == "Socket":
            return self.inst.query(command)
        else:
            raise NotImplementedError(f"Query method for {self.conn_type} not found.")

    def write_device(self, command: str):
        """
        Send a write command to the device.

        :param command: SCPI command to be sent to the device.
        """
        self.inst.write(command)

    def close(self):
        """
        Close the opened device and any associated resources
        """
        self.inst.close()
        if self.conn_type == "VISA":
            self.rm.close()

    def id(self) -> dict:
        """
        Query the ID string of the instrument

        :return: Dictionary containing the manufacturer, instrument model,
                 serial number, and version number.
        :rtype: dict
        """
        id_str = self.query_device("*IDN?").strip().split(",")
        logging.debug(f"id_str: {id_str}")
        return {
            "manufacturer": id_str[0],
            "model": id_str[1],
            "serial_number": id_str[2],
            "version": id_str[3],
        }
