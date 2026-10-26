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

#if !DT_NODE_EXISTS(DAC_U1_NODE)
#error "dacset needs a DACx578 node labelled dac_u1 (and optionally dac_u2)"
#endif

/* A rig is built with one DAC or two. dac_u1 always serves board channels
 * 0..7; dac_u2, when the devicetree has one, adds 8..14. Which addresses they
 * sit at is the overlay's business, not this file's.
 */
#define HAVE_DAC_U2 DT_NODE_EXISTS(DAC_U2_NODE)

#if HAVE_DAC_U2
/* The channel map below assumes the two parts are interchangeable. They are on
 * this board, but say so at compile time rather than trusting the overlay.
 */
BUILD_ASSERT(DT_PROP(DAC_U1_NODE, resolution) == DT_PROP(DAC_U2_NODE, resolution),
	     "both DACs must share a resolution");
BUILD_ASSERT(DT_PROP(DAC_U1_NODE, full_scale_mv) == DT_PROP(DAC_U2_NODE, full_scale_mv),
	     "both DACs must share a full-scale range");
#endif

#define DAC_RESOLUTION    DT_PROP(DAC_U1_NODE, resolution)
#define DAC_FULL_SCALE_MV DT_PROP(DAC_U1_NODE, full_scale_mv)
#define DAC_CH_PER_PART   8
#define BOARD_NUM_CH      (HAVE_DAC_U2 ? 15 : DAC_CH_PER_PART)

static const struct device *dac_for_channel(unsigned int ch, uint8_t *local)
{
	if (ch < DAC_CH_PER_PART) {
		*local = (uint8_t)ch;
		return DEVICE_DT_GET(DAC_U1_NODE);
	}

#if HAVE_DAC_U2
	*local = (uint8_t)(ch - DAC_CH_PER_PART);
	return DEVICE_DT_GET(DAC_U2_NODE);
#else
	/* Unreachable: cmd_dacset rejects ch >= BOARD_NUM_CH first. */
	*local = 0;
	return NULL;
#endif
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
		shell_error(sh, "channel must be 0..%d - this board is built with %d DAC%s",
			    BOARD_NUM_CH - 1, HAVE_DAC_U2 ? 2 : 1, HAVE_DAC_U2 ? "s" : "");
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
			    ret == -EIO ? " - nothing answered on the bus" : "");
		return ret;
	}

	shell_print(sh, "dacset ch%u -> %s ch%u code=%u OK",
		    ch, dev->name, local, code);
	return 0;
}

SHELL_CMD_ARG_REGISTER(dacset, NULL,
		       "Drive a DAC channel across the virtual PCB: dacset <ch> <mv>",
		       cmd_dacset, 3, 0);
