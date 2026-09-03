/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shell command that drives a real DAC7578 register write. Unlike the
 * simulator target's `adcset`, nothing is injected into the ADC: the value
 * travels MCU -> DAC driver -> I2C -> DAC -> net -> ADC. Against the virtual
 * PCB two of those hops cross a process boundary; on hardware they cross
 * copper. The firmware cannot tell, which is the point.
 *
 * This command is deliberately target-agnostic. The register encoding lives in
 * the DACx578 driver, and all that is left here is the board-level channel map
 * and the millivolt-to-code conversion, so wiring the parts up for real needs
 * no new code.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#define DAC_U1_NODE DT_NODELABEL(dac_u1)
#define DAC_U2_NODE DT_NODELABEL(dac_u2)

#if !DT_NODE_EXISTS(DAC_U1_NODE) || !DT_NODE_EXISTS(DAC_U2_NODE)
#error "dacset needs two DACx578 nodes labelled dac_u1 and dac_u2"
#endif

/* The channel map below assumes the two parts are interchangeable. They are on
 * this board, but say so at compile time rather than trusting the overlay.
 */
BUILD_ASSERT(DT_PROP(DAC_U1_NODE, resolution) == DT_PROP(DAC_U2_NODE, resolution),
	     "both DACs must share a resolution");
BUILD_ASSERT(DT_PROP(DAC_U1_NODE, full_scale_mv) == DT_PROP(DAC_U2_NODE, full_scale_mv),
	     "both DACs must share a full-scale range");

#define DAC_RESOLUTION    DT_PROP(DAC_U1_NODE, resolution)
#define DAC_FULL_SCALE_MV DT_PROP(DAC_U1_NODE, full_scale_mv)
#define DAC_CH_PER_PART   8
#define BOARD_NUM_CH      15

/* Two parts cover 15 channels: 0x48 -> ch 0..7, 0x4c -> ch 8..14 */
static const struct device *dac_for_channel(unsigned int ch, uint8_t *local)
{
	if (ch < DAC_CH_PER_PART) {
		*local = (uint8_t)ch;
		return DEVICE_DT_GET(DAC_U1_NODE);
	}

	*local = (uint8_t)(ch - DAC_CH_PER_PART);
	return DEVICE_DT_GET(DAC_U2_NODE);
}

static int cmd_dacset(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	struct dac_channel_cfg cfg;
	unsigned int ch;
	int mv, ret;
	uint8_t local;
	uint16_t code;

	if (argc != 3) {
		shell_error(sh, "usage: dacset <channel> <millivolts>");
		return -EINVAL;
	}

	ch = (unsigned int)strtoul(argv[1], NULL, 0);
	mv = (int)strtol(argv[2], NULL, 0);

	if (ch >= BOARD_NUM_CH) {
		shell_error(sh, "channel must be 0..%d", BOARD_NUM_CH - 1);
		return -EINVAL;
	}
	if (mv < 0 || mv > DAC_FULL_SCALE_MV) {
		shell_error(sh, "millivolts must be 0..%d", DAC_FULL_SCALE_MV);
		return -EINVAL;
	}

	dev = dac_for_channel(ch, &local);
	if (!device_is_ready(dev)) {
		shell_error(sh, "%s not ready", dev->name);
		return -ENODEV;
	}

	/* Costs no bus traffic on this part, so it is done per command rather
	 * than tracked in state the shell would have to invalidate.
	 */
	cfg = (struct dac_channel_cfg){
		.channel_id = local,
		.resolution = DAC_RESOLUTION,
	};

	ret = dac_channel_setup(dev, &cfg);
	if (ret != 0) {
		shell_error(sh, "dacset ch%u: channel setup failed (%d)", ch, ret);
		return ret;
	}

	code = (uint16_t)(((long)mv * ((1 << DAC_RESOLUTION) - 1)) / DAC_FULL_SCALE_MV);

	ret = dac_write_value(dev, local, code);
	if (ret != 0) {
		shell_error(sh,
			    "dacset ch%u: write to %s FAILED (%d)%s",
			    ch, dev->name, ret,
			    ret == -ENODEV ? " - device did not ACK" : "");
		return ret;
	}

	shell_print(sh, "dacset ch%u -> %s ch%u code=%u OK",
		    ch, dev->name, local, code);
	return 0;
}

SHELL_CMD_ARG_REGISTER(dacset, NULL,
		       "Drive a DAC channel across the virtual PCB: dacset <ch> <mv>",
		       cmd_dacset, 3, 0);
