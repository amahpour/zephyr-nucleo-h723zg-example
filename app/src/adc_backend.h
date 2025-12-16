/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC Backend Interface - Target-agnostic ADC abstraction
 *
 * This header defines the interface that must be implemented by
 * target-specific backends (SIM or HW).
 */

#ifndef ADC_BACKEND_H_
#define ADC_BACKEND_H_

#include <stdint.h>
#include "regs.h"  /* For NUM_CH */

/**
 * @brief Initialize the ADC backend
 *
 * Must be called once during startup before any sampling.
 *
 * @return 0 on success, negative errno on failure
 */
int adc_backend_init(void);

/**
 * @brief Sample all ADC channels
 *
 * Reads all configured channels and returns values in millivolts.
 *
 * @param out_mv Array of NUM_CH to receive millivolt values
 * @return 0 on success, negative errno on failure
 */
int adc_backend_sample_all(int32_t out_mv[NUM_CH]);

#endif /* ADC_BACKEND_H_ */

