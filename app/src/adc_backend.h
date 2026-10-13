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
 * Reads all configured channels and returns values in millivolts, with a
 * per-channel flag saying whether each value is a real reading. A channel
 * whose read failed - or, on the virtual PCB, whose net is floating - comes
 * back invalid instead of as a made-up 0 mV.
 *
 * @param out_mv    Array of NUM_CH to receive millivolt values
 * @param out_valid Array of NUM_CH: true where out_mv[i] is a real reading
 * @return 0 on success, negative errno on failure
 */
int adc_backend_sample_all(int32_t out_mv[NUM_CH], bool out_valid[NUM_CH]);

#endif /* ADC_BACKEND_H_ */

