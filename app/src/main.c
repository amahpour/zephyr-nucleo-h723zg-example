/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC Sampler Application - Main Entry Point
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "regs.h"
#include "adc_backend.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Sampling thread configuration */
#define SAMPLE_THREAD_STACK_SIZE 1024
#define SAMPLE_THREAD_PRIORITY   5

#ifdef CONFIG_APP_SAMPLE_PERIOD_MS
#define SAMPLE_PERIOD_MS CONFIG_APP_SAMPLE_PERIOD_MS
#else
#define SAMPLE_PERIOD_MS 100
#endif

/* Sampling thread stack and data */
K_THREAD_STACK_DEFINE(sample_thread_stack, SAMPLE_THREAD_STACK_SIZE);
static struct k_thread sample_thread_data;

/**
 * @brief Sampling thread entry point
 *
 * Periodically samples all ADC channels and updates the register file.
 */
static void sample_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int32_t samples[NUM_CH];
    int ret;

    LOG_INF("Sampling thread started (period=%d ms)", SAMPLE_PERIOD_MS);

    while (1) {
        ret = adc_backend_sample_all(samples);
        if (ret == 0) {
            regs_update(samples);
        } else {
            LOG_ERR("ADC sample failed: %d", ret);
        }

        k_msleep(SAMPLE_PERIOD_MS);
    }
}

int main(void)
{
    int ret;

    LOG_INF("ADC Sampler application started");

    /* Initialize register file */
    regs_init();

    /* Initialize ADC backend */
    ret = adc_backend_init();
    if (ret != 0) {
        LOG_ERR("ADC backend init failed: %d", ret);
        return ret;
    }

    /* Start sampling thread */
    k_thread_create(&sample_thread_data, sample_thread_stack,
                    K_THREAD_STACK_SIZEOF(sample_thread_stack),
                    sample_thread_entry,
                    NULL, NULL, NULL,
                    SAMPLE_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&sample_thread_data, "adc_sampler");

    printk("ADC Sampler ready. Type 'help' for available commands.\n");

    return 0;
}

