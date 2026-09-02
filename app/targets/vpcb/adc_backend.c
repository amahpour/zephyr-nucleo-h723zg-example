/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC Backend - Virtual PCB target.
 *
 * The ADC is internal to the MCU, so it stays inside the process and is
 * modelled with adc_emul. What changes is where its input comes from: each
 * channel's value function samples a virtual PCB net, i.e. the copper trace
 * driven by a DAC that lives in another process entirely.
 */

#include "../../src/adc_backend.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/logging/log.h>

#include "i2c_vpcb.h"

LOG_MODULE_REGISTER(adc_backend_vpcb, LOG_LEVEL_INF);

#define ADC_NODE DT_NODELABEL(adc0)

#if !DT_NODE_EXISTS(ADC_NODE)
#error "ADC node 'adc0' not found in devicetree. Check your overlay."
#endif

static const struct device *adc_dev;
static struct adc_channel_cfg channel_cfgs[NUM_CH];
static int16_t sample_buffer[NUM_CH];

#define ADC_REF_MV     3300
#define ADC_RESOLUTION 12

/* Called by adc_emul on every read: fetch the net this pin is wired to. */
static int vpcb_value_func(const struct device *dev, unsigned int chan,
			   void *data, uint32_t *result)
{
	int32_t mv = 0;
	int ret;

	ARG_UNUSED(dev);
	ARG_UNUSED(data);

	ret = vpcb_net_read_mv(chan, &mv);
	if (ret != 0) {
		/* Net unreadable - report 0 V but say so, loudly. */
		LOG_WRN("net %u unreadable (%d)", chan, ret);
		*result = 0;
		return 0;
	}
	*result = (uint32_t)mv;
	return 0;
}

int adc_backend_init(void)
{
	int ret;

	adc_dev = DEVICE_DT_GET(ADC_NODE);
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	for (int i = 0; i < NUM_CH; i++) {
		channel_cfgs[i] = (struct adc_channel_cfg){
			.gain = ADC_GAIN_1,
			.reference = ADC_REF_INTERNAL,
			.acquisition_time = ADC_ACQ_TIME_DEFAULT,
			.channel_id = i,
		};

		ret = adc_channel_setup(adc_dev, &channel_cfgs[i]);
		if (ret < 0) {
			LOG_ERR("Failed to setup channel %d: %d", i, ret);
			return ret;
		}

		ret = adc_emul_value_func_set(adc_dev, i, vpcb_value_func, NULL);
		if (ret < 0) {
			LOG_ERR("Failed to bind net for ch %d: %d", i, ret);
			return ret;
		}
	}

	LOG_INF("ADC backend (VPCB) initialized with %d channels", NUM_CH);
	return 0;
}

int adc_backend_sample_all(int32_t out_mv[NUM_CH])
{
	int ret;

	for (int i = 0; i < NUM_CH; i++) {
		struct adc_sequence sequence = {
			.buffer = &sample_buffer[i],
			.buffer_size = sizeof(sample_buffer[i]),
			.resolution = ADC_RESOLUTION,
			.channels = BIT(i),
		};

		ret = adc_read(adc_dev, &sequence);
		if (ret < 0) {
			LOG_ERR("ADC read failed for channel %d: %d", i, ret);
			out_mv[i] = 0;
		} else {
			int32_t raw = sample_buffer[i];
			out_mv[i] = (raw * ADC_REF_MV) / ((1 << ADC_RESOLUTION) - 1);
		}
	}

	return 0;
}
