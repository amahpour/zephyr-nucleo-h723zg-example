/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interface between the Zephyr "top" half and the host "bottom" half.
 * Only fixed-width types: this header is included from both contexts.
 */
#ifndef I2C_VPCB_BOTTOM_H_
#define I2C_VPCB_BOTTOM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return values mirror enum vpcb_status; negative means transport failure.
 *
 * VPCB_BOTTOM_NO_REPLY is deliberately NOT one of the vpcb_status values. A
 * peer that goes silent is a broken harness, not a bus event, and the two must
 * stay distinguishable: reporting a wedged model as a bus timeout is the one
 * lie this whole out-of-process design exists to prevent.
 */
#define VPCB_BOTTOM_NO_REPLY (-2)

int  vpcb_bottom_connect(const char *path, const char *name);
int  vpcb_bottom_i2c(uint16_t addr, const uint8_t *w, uint16_t wlen,
		     uint8_t *r, uint16_t rlen, int timeout_ms);
int  vpcb_bottom_net_get(uint32_t net, int32_t *microvolts, int timeout_ms);
void vpcb_bottom_close(void);

#ifdef __cplusplus
}
#endif
#endif /* I2C_VPCB_BOTTOM_H_ */
