/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Texas Instruments DACx578: 8-channel I2C DAC (DAC5578/6578/7578).
 *
 * The family shares one register map and differs only in resolution, so the
 * part is selected from devicetree rather than by compile-time constant.
 *
 * Register map cross-checked against the TI datasheet: the command and access
 * byte is C3:C0 | A3:A0, and the data word is left-justified into MSDB/LSDB
 * by (16 - resolution) bits.
 *
 * Nothing here is specific to the virtual PCB. The driver talks to whatever
 * I2C controller the devicetree gives it, so the same object code runs against
 * an out-of-process model and against real silicon.
 */

#define DT_DRV_COMPAT ti_dacx578

#include <zephyr/device.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dac_dacx578, CONFIG_DAC_LOG_LEVEL);

#define DACX578_NUM_CHANNELS 8

/* Command codes, C3:C0 */
#define DACX578_CMD_WRITE_INPUT      0x0
#define DACX578_CMD_UPDATE_DAC       0x1
#define DACX578_CMD_WRITE_UPDATE_ALL 0x2
#define DACX578_CMD_WRITE_UPDATE_CH  0x3

struct dacx578_config {
	struct i2c_dt_spec bus;
	uint8_t resolution;
};

static int dacx578_channel_setup(const struct device *dev,
				 const struct dac_channel_cfg *channel_cfg)
{
	const struct dacx578_config *config = dev->config;

	if (channel_cfg->channel_id >= DACX578_NUM_CHANNELS) {
		LOG_ERR("channel %u out of range (0-%u)",
			channel_cfg->channel_id, DACX578_NUM_CHANNELS - 1);
		return -EINVAL;
	}

	if (channel_cfg->resolution != config->resolution) {
		LOG_ERR("resolution %u not supported, this part is %u-bit",
			channel_cfg->resolution, config->resolution);
		return -ENOTSUP;
	}

	/* Channels need no per-channel setup: they power up in normal mode
	 * with the LDAC mask defaulting to immediate update.
	 */
	return 0;
}

static int dacx578_write_value(const struct device *dev, uint8_t channel,
			       uint32_t value)
{
	const struct dacx578_config *config = dev->config;
	uint16_t data;
	uint8_t buf[3];

	if (channel >= DACX578_NUM_CHANNELS) {
		LOG_ERR("channel %u out of range", channel);
		return -EINVAL;
	}

	if (value > BIT_MASK(config->resolution)) {
		LOG_ERR("value %u exceeds %u-bit range", value, config->resolution);
		return -EINVAL;
	}

	/* Left-justify the code into the two data bytes. */
	data = (uint16_t)(value << (16U - config->resolution));

	buf[0] = (DACX578_CMD_WRITE_UPDATE_CH << 4) | (channel & 0x0F);
	buf[1] = (uint8_t)(data >> 8);
	buf[2] = (uint8_t)(data & 0xFF);

	return i2c_write_dt(&config->bus, buf, sizeof(buf));
}

static int dacx578_init(const struct device *dev)
{
	const struct dacx578_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_ERR("I2C bus %s not ready", config->bus.bus->name);
		return -ENODEV;
	}

	/* Deliberately no probe transaction here: on a bus whose peers may
	 * legitimately be absent, refusing to initialise would turn a missing
	 * chip into a boot failure instead of a reportable I/O error.
	 */
	LOG_INF("DACx578 at 0x%02x, %u-bit", config->bus.addr, config->resolution);
	return 0;
}

static DEVICE_API(dac, dacx578_driver_api) = {
	.channel_setup = dacx578_channel_setup,
	.write_value   = dacx578_write_value,
};

#define DACX578_INIT(n)								\
	static const struct dacx578_config dacx578_config_##n = {		\
		.bus = I2C_DT_SPEC_INST_GET(n),					\
		.resolution = DT_INST_PROP(n, resolution),			\
	};									\
										\
	DEVICE_DT_INST_DEFINE(n, dacx578_init, NULL,				\
			      NULL, &dacx578_config_##n,			\
			      POST_KERNEL, CONFIG_DAC_INIT_PRIORITY,		\
			      &dacx578_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DACX578_INIT)
