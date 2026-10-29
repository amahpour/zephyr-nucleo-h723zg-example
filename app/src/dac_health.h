/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_DAC_HEALTH_H_
#define APP_DAC_HEALTH_H_

#include <stdbool.h>
#include "regs.h"  /* NUM_CH */

#ifdef CONFIG_DAC_DACX578
/**
 * @brief Mark the channels of any DAC that no longer answers as not valid.
 */
void dac_health_apply(bool valid[NUM_CH]);
#else
/* No DACs on this target (QEMU): nothing drives the channels but injection. */
static inline void dac_health_apply(bool valid[NUM_CH]) { (void)valid; }
#endif

#endif /* APP_DAC_HEALTH_H_ */
