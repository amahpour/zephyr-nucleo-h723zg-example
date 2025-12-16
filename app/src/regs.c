/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC Register File Implementation
 */

#include "regs.h"
#include <string.h>

/* Mutex protecting the register file */
static K_MUTEX_DEFINE(regs_mutex);

/* The register file instance */
static struct adc_regs regs;

void regs_init(void)
{
    k_mutex_lock(&regs_mutex, K_FOREVER);

    memset(&regs, 0, sizeof(regs));
    regs.seq = 0;
    regs.last_sample_uptime_ms = 0;

    k_mutex_unlock(&regs_mutex);
}

void regs_update(const int32_t mv[NUM_CH])
{
    k_mutex_lock(&regs_mutex, K_FOREVER);

    for (int i = 0; i < NUM_CH; i++) {
        regs.mv[i] = mv[i];
    }
    regs.seq++;
    regs.last_sample_uptime_ms = k_uptime_get();

    k_mutex_unlock(&regs_mutex);
}

void regs_read(struct adc_regs *out)
{
    k_mutex_lock(&regs_mutex, K_FOREVER);

    memcpy(out, &regs, sizeof(regs));

    k_mutex_unlock(&regs_mutex);
}

