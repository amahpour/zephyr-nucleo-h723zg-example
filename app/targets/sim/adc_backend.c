/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC Backend - Simulator Implementation using adc-emul
 */

#include "../../src/adc_backend.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc_backend_sim, LOG_LEVEL_INF);

/* Get ADC device from devicetree */
#define ADC_NODE DT_NODELABEL(adc0)

#if !DT_NODE_EXISTS(ADC_NODE)
#error "ADC node 'adc0' not found in devicetree. Check your overlay."
#endif

static const struct device *adc_dev;

/* ADC channel configuration */
static struct adc_channel_cfg channel_cfgs[NUM_CH];
static int16_t sample_buffer[NUM_CH];

/* Reference voltage in mV */
#define ADC_REF_MV 3300

/* ADC resolution (12-bit typical for emulator) */
#define ADC_RESOLUTION 12

/* Injected values for each channel (used by adcset command) */
static int32_t injected_mv[NUM_CH];
static bool injection_enabled[NUM_CH];

int adc_backend_init(void)
{
    int ret;

    adc_dev = DEVICE_DT_GET(ADC_NODE);
    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    /* Configure all channels */
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

        /* Set initial emulator value (mid-scale) */
        ret = adc_emul_const_value_set(adc_dev, i, 1650);
        if (ret < 0) {
            LOG_WRN("Failed to set initial emulator value for ch %d: %d", i, ret);
        }

        injected_mv[i] = 1650;
        injection_enabled[i] = false;
    }

    LOG_INF("ADC backend (SIM) initialized with %d channels", NUM_CH);

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
            /* Convert raw value to millivolts */
            int32_t raw = sample_buffer[i];
            out_mv[i] = (raw * ADC_REF_MV) / ((1 << ADC_RESOLUTION) - 1);
        }
    }

    return 0;
}

/**
 * @brief Inject a millivolt value for a channel (SIM only)
 *
 * This sets the emulator to return this value on subsequent reads.
 *
 * @param ch Channel number (0 to NUM_CH-1)
 * @param mv Millivolt value to inject
 * @return 0 on success, negative errno on failure
 */
int adc_backend_inject_mv(unsigned int ch, int32_t mv)
{
    int ret;

    if (ch >= NUM_CH) {
        return -EINVAL;
    }

    if (mv < 0 || mv > ADC_REF_MV) {
        LOG_WRN("Clamping injection value %d to [0, %d]", mv, ADC_REF_MV);
        mv = (mv < 0) ? 0 : ADC_REF_MV;
    }

    /* Set the emulator constant value */
    ret = adc_emul_const_value_set(adc_dev, ch, mv);
    if (ret < 0) {
        LOG_ERR("Failed to inject value for channel %d: %d", ch, ret);
        return ret;
    }

    injected_mv[ch] = mv;
    injection_enabled[ch] = true;

    LOG_INF("Injected ch[%u] = %d mV", ch, mv);

    return 0;
}
