/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC Register File - Thread-safe storage for ADC samples
 */

#ifndef REGS_H_
#define REGS_H_

#include <zephyr/kernel.h>
#include <stdint.h>

#ifdef CONFIG_APP_NUM_CH
#define NUM_CH CONFIG_APP_NUM_CH
#else
#define NUM_CH 4
#endif

/**
 * @brief ADC register file structure
 *
 * Stores the latest ADC sample values and metadata.
 * Access must be protected by the regs API functions.
 */
struct adc_regs {
    int32_t mv[NUM_CH];             /* Latest mV per channel */
    uint32_t seq;                   /* Sequence number, increments each update */
    int64_t last_sample_uptime_ms;  /* Timestamp of last update (k_uptime_get()) */
};

/**
 * @brief Initialize the register file
 *
 * Must be called before any other regs_* functions.
 */
void regs_init(void);

/**
 * @brief Update the register file with new samples
 *
 * Thread-safe. Updates all channel values atomically.
 *
 * @param mv Array of NUM_CH millivolt values
 */
void regs_update(const int32_t mv[NUM_CH]);

/**
 * @brief Read the current register file state
 *
 * Thread-safe. Copies the current state to the provided structure.
 *
 * @param out Pointer to structure to receive the current state
 */
void regs_read(struct adc_regs *out);

#endif /* REGS_H_ */

