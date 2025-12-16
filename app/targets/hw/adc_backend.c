/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC Backend - Hardware Implementation for NUCLEO-H723ZG
 *
 * TODO: This is a skeleton for real ADC hardware support.
 * Once hardware arrives, configure ADC channels and implement
 * actual sampling using the Zephyr ADC API.
 *
 * NUCLEO-H723ZG (STM32H723ZG) ADC Resources:
 * - ADC1, ADC2, ADC3 available
 * - 16-bit resolution capable
 * - Multiple channels per ADC
 *
 * Common Arduino-header ADC pins:
 * - A0: PA3  (ADC1_INP15 or ADC2_INP15)
 * - A1: PC0  (ADC1_INP10 or ADC2_INP10)
 * - A2: PC3  (ADC1_INP13 or ADC2_INP13)
 * - A3: PB1  (ADC1_INP5)
 * - A4: PC2  (ADC1_INP12 or ADC2_INP12)
 * - A5: PF10 (ADC3_INP6)
 *
 * See the board devicetree for exact channel mappings:
 *   zephyr/boards/st/nucleo_h723zg/nucleo_h723zg.dts
 */

#include "../../src/adc_backend.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc_backend_hw, LOG_LEVEL_INF);

/*
 * TODO: Define the ADC device node label once hardware is configured.
 *
 * Example for STM32:
 *   #define ADC_NODE DT_NODELABEL(adc1)
 *
 * You may also need a devicetree overlay for your specific pin mapping.
 */

/* Placeholder: set to 1 when ADC is properly configured */
#define ADC_CONFIGURED 0

#if ADC_CONFIGURED

#define ADC_NODE DT_NODELABEL(adc1)

static const struct device *adc_dev;
static struct adc_channel_cfg channel_cfgs[NUM_CH];
static int16_t sample_buffer[NUM_CH];

#define ADC_RESOLUTION 12
#define ADC_REF_MV     3300

#endif /* ADC_CONFIGURED */

int adc_backend_init(void)
{
#if ADC_CONFIGURED
    int ret;

    adc_dev = DEVICE_DT_GET(ADC_NODE);
    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    /*
     * TODO: Configure channels based on your wiring.
     *
     * Example:
     *   channel_cfgs[0] = (struct adc_channel_cfg){
     *       .gain = ADC_GAIN_1,
     *       .reference = ADC_REF_INTERNAL,
     *       .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 480),
     *       .channel_id = 15,  // PA3 = ADC1_INP15
     *   };
     */

    for (int i = 0; i < NUM_CH; i++) {
        channel_cfgs[i] = (struct adc_channel_cfg){
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = i,  /* TODO: Map to actual hardware channels */
        };

        ret = adc_channel_setup(adc_dev, &channel_cfgs[i]);
        if (ret < 0) {
            LOG_ERR("Failed to setup channel %d: %d", i, ret);
            return ret;
        }
    }

    LOG_INF("ADC backend (HW) initialized with %d channels", NUM_CH);
    return 0;

#else
    LOG_WRN("ADC backend (HW) not configured - returning stub data");
    LOG_WRN("Set ADC_CONFIGURED=1 after configuring devicetree");
    return 0;
#endif
}

int adc_backend_sample_all(int32_t out_mv[NUM_CH])
{
#if ADC_CONFIGURED
    int ret;

    for (int i = 0; i < NUM_CH; i++) {
        struct adc_sequence sequence = {
            .buffer = &sample_buffer[i],
            .buffer_size = sizeof(sample_buffer[i]),
            .resolution = ADC_RESOLUTION,
            .channels = BIT(channel_cfgs[i].channel_id),
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

#else
    /* Return zeros when not configured */
    for (int i = 0; i < NUM_CH; i++) {
        out_mv[i] = 0;
    }
    return 0;
#endif
}
