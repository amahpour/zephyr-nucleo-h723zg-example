/* SPDX-License-Identifier: Apache-2.0 */
#ifndef I2C_VPCB_H_
#define I2C_VPCB_H_

#include <stdint.h>

/* Sample a virtual PCB net (the analog trace the MCU's ADC pin sits on). */
int vpcb_net_read_mv(uint32_t net, int32_t *millivolts);

const char *vpcb_sock(void);
void vpcb_set_sock(const char *path);

#endif /* I2C_VPCB_H_ */
