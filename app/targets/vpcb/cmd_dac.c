/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shell command that drives a real DAC7578 register write across the virtual
 * PCB. Unlike the simulator target's `adcset`, nothing is injected into the
 * ADC: the value travels MCU -> I2C -> board -> IC process -> net -> ADC,
 * the same path it takes on hardware.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#define VPCB_I2C_NODE DT_NODELABEL(vpcb_i2c)

/* DACx578 command codes (C3:C0), see DAC5578.h / datasheet table_0022 */
#define CMD_WRITE_UPDATE_CH 0x3
#define DAC_RESOLUTION      12
#define DAC_FULL_SCALE_MV   3300

/* Two parts cover 15 channels: 0x48 -> ch 0..7, 0x4c -> ch 8..14 */
static uint16_t addr_for_channel(unsigned int ch, uint8_t *local)
{
	if (ch < 8) { *local = (uint8_t)ch;     return 0x48; }
	*local = (uint8_t)(ch - 8);             return 0x4c;
}

static int cmd_dacset(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *bus = DEVICE_DT_GET(VPCB_I2C_NODE);
	unsigned int ch;
	int mv, ret;
	uint8_t local;
	uint16_t addr, code, data;
	uint8_t w[3];

	if (argc != 3) {
		shell_error(sh, "usage: dacset <channel> <millivolts>");
		return -EINVAL;
	}

	ch = (unsigned int)strtoul(argv[1], NULL, 0);
	mv = (int)strtol(argv[2], NULL, 0);

	if (ch >= 15) {
		shell_error(sh, "channel must be 0..14");
		return -EINVAL;
	}
	if (mv < 0 || mv > DAC_FULL_SCALE_MV) {
		shell_error(sh, "millivolts must be 0..%d", DAC_FULL_SCALE_MV);
		return -EINVAL;
	}

	if (!device_is_ready(bus)) {
		shell_error(sh, "virtual PCB I2C bus not ready");
		return -ENODEV;
	}

	addr = addr_for_channel(ch, &local);

	/* 12-bit code, left-justified across MSDB/LSDB */
	code = (uint16_t)(((long)mv * ((1 << DAC_RESOLUTION) - 1)) / DAC_FULL_SCALE_MV);
	data = (uint16_t)(code << (16 - DAC_RESOLUTION));

	w[0] = (uint8_t)((CMD_WRITE_UPDATE_CH << 4) | (local & 0x0F));
	w[1] = (uint8_t)(data >> 8);
	w[2] = (uint8_t)(data & 0xFF);

	ret = i2c_write(bus, w, sizeof(w), addr);
	if (ret != 0) {
		shell_error(sh,
			    "dacset ch%u: I2C write to 0x%02x FAILED (%d)%s",
			    ch, addr, ret,
			    ret == -ENODEV ? " - device did not ACK" : "");
		return ret;
	}

	shell_print(sh, "dacset ch%u -> dac@0x%02x ch%u code=%u (%02X %02X %02X) OK",
		    ch, addr, local, code, w[0], w[1], w[2]);
	return 0;
}

SHELL_CMD_ARG_REGISTER(dacset, NULL,
		       "Drive a DAC channel across the virtual PCB: dacset <ch> <mv>",
		       cmd_dacset, 3, 0);
