/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * A reading is only as good as what drives it. On this rig every wired ADC
 * channel is driven by a DACx578, so each sample cycle asks each DAC whether it
 * still answers. If one doesn't, the channels it drives are marked "no
 * reading" (--- in adcregs) until it answers again.
 *
 * The pin may still hold a perfectly good voltage - pull SDA and the DAC keeps
 * driving its last value - but the firmware has lost the part that set it, so
 * it can no longer vouch for the number. This is the same on the real board
 * and the virtual one, because it is decided here, from the same I2C answer.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "../drivers/dac_dacx578.h"
#include "dac_health.h"

LOG_MODULE_REGISTER(dac_health, LOG_LEVEL_INF);

#define DAC_CH_PER_PART 8

struct dac_part {
	const struct device *dev;
	unsigned int first_ch;   /* board channel driven by output 0 */
	bool answering;
};

static struct dac_part parts[] = {
	{ DEVICE_DT_GET(DT_NODELABEL(dac_u1)), 0, true },
#if DT_NODE_EXISTS(DT_NODELABEL(dac_u2))
	{ DEVICE_DT_GET(DT_NODELABEL(dac_u2)), DAC_CH_PER_PART, true },
#endif
};

void dac_health_apply(bool valid[NUM_CH])
{
	for (size_t i = 0; i < ARRAY_SIZE(parts); i++) {
		struct dac_part *p = &parts[i];
		int ret = dacx578_check(p->dev);
		bool now = (ret == 0);

		/* Once per change of state, not ten times a second. */
		if (now != p->answering) {
			if (now) {
				LOG_INF("%s answering again", p->dev->name);
			} else {
				LOG_WRN("%s not answering (%d) - its channels read ---",
					p->dev->name, ret);
			}
			p->answering = now;
		}

		if (!now) {
			for (unsigned int ch = p->first_ch;
			     ch < p->first_ch + DAC_CH_PER_PART && ch < NUM_CH; ch++) {
				valid[ch] = false;
			}
		}
	}
}
