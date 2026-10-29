/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DACx578 calls beyond the Zephyr DAC API.
 */

#ifndef APP_DRIVERS_DAC_DACX578_H_
#define APP_DRIVERS_DAC_DACX578_H_

#include <zephyr/device.h>

/**
 * @brief Ask the part whether it is still there, without changing an output.
 *
 * Reads back a DAC register over I2C.
 *
 * @return 0 if the part answered, or the I2C error (-EIO on the STM32 driver
 *         and the virtual board alike) if it did not.
 */
int dacx578_check(const struct device *dev);

#endif /* APP_DRIVERS_DAC_DACX578_H_ */
