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
 * ADC channel mapping for 15-channel mux setup:
 * Channel 0-14 map to mux outputs C0-C14
 *
 * ADC1 channels: INP3, INP5, INP9, INP10, INP15, INP16, INP18, INP19 (8 channels)
 * ADC3 channels: INP0, INP1, INP4, INP5, INP6, INP8, INP9 (7 channels)
 *
 * NOTE: PC2 and PC3 are PC2_C/PC3_C pins that only connect to ADC3!
 */

#define ADC_CONFIGURED 1

#if ADC_CONFIGURED

#define ADC1_NODE DT_NODELABEL(adc1)
#define ADC3_NODE DT_NODELABEL(adc3)

static const struct device *adc1_dev;
static const struct device *adc3_dev;
static struct adc_channel_cfg channel_cfgs[NUM_CH];
static int16_t sample_buffer[NUM_CH];

#define ADC_RESOLUTION 12
#define ADC_REF_MV     3300

/* Map software channel (0-14) to ADC peripheral and hardware channel ID */
struct channel_map {
    const struct device **dev;  /* Pointer to ADC device */
    uint8_t channel_id;          /* Hardware ADC channel ID */
};

static struct channel_map channel_mappings[NUM_CH] = {
    /* C0: PA3 -> ADC1_INP15 */
    { &adc1_dev, 15 },
    /* C1: PC0 -> ADC1_INP10 */
    { &adc1_dev, 10 },
    /* C2: PC3 -> ADC3_INP1 (PC3_C is ADC3 only!) */
    { &adc3_dev, 1 },
    /* C3: PB1 -> ADC1_INP5 */
    { &adc1_dev, 5 },
    /* C4: PC2 -> ADC3_INP0 (PC2_C is ADC3 only!) */
    { &adc3_dev, 0 },
    /* C5: PF10 -> ADC3_INP6 */
    { &adc3_dev, 6 },
    /* C6: PA5 -> ADC1_INP19 */
    { &adc1_dev, 19 },
    /* C7: PA6 -> ADC1_INP3 */
    { &adc1_dev, 3 },
    /* C8: PA4 -> ADC1_INP18 */
    { &adc1_dev, 18 },
    /* C9: PF3 -> ADC3_INP5 */
    { &adc3_dev, 5 },
    /* C10: PF4 -> ADC3_INP9 */
    { &adc3_dev, 9 },
    /* C11: PF5 -> ADC3_INP4 */
    { &adc3_dev, 4 },
    /* C12: PF6 -> ADC3_INP8 */
    { &adc3_dev, 8 },
    /* C13: PA0 -> ADC1_INP16 */
    { &adc1_dev, 16 },
    /* C14: PB0 -> ADC1_INP9 */
    { &adc1_dev, 9 },
};

#endif /* ADC_CONFIGURED */

int adc_backend_init(void)
{
#if ADC_CONFIGURED
    int ret;

    /* Initialize ADC1 */
    adc1_dev = DEVICE_DT_GET(ADC1_NODE);
    if (!device_is_ready(adc1_dev)) {
        LOG_ERR("ADC1 device not ready");
        return -ENODEV;
    }

    /* Initialize ADC3 */
    adc3_dev = DEVICE_DT_GET(ADC3_NODE);
    if (!device_is_ready(adc3_dev)) {
        LOG_ERR("ADC3 device not ready");
        return -ENODEV;
    }

    /* Configure all channels */
    for (int i = 0; i < NUM_CH; i++) {
        channel_cfgs[i] = (struct adc_channel_cfg){
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,  /* STM32 uses VREF+ pin (3.3V) */
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,  /* Use driver default */
            .channel_id = channel_mappings[i].channel_id,
            .differential = 0,  /* Single-ended mode */
        };

        /* Setup channel on the appropriate ADC device */
        ret = adc_channel_setup(*channel_mappings[i].dev, &channel_cfgs[i]);
        if (ret < 0) {
            LOG_ERR("Failed to setup channel %d (ADC channel %d): %d", 
                    i, channel_mappings[i].channel_id, ret);
            return ret;
        }
        LOG_INF("Channel %d setup OK: ADC%d ch%d", i, 
                (channel_mappings[i].dev == &adc1_dev) ? 1 : 3,
                channel_mappings[i].channel_id);
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

        /* Read from the appropriate ADC device */
        ret = adc_read(*channel_mappings[i].dev, &sequence);
        if (ret < 0) {
            LOG_ERR("ADC read failed for channel %d (ADC channel %d): %d", 
                    i, channel_cfgs[i].channel_id, ret);
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
