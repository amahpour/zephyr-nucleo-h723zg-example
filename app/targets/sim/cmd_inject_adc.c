/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shell command: adcset - Inject ADC values (SIM only)
 *
 * This command is only compiled for simulator builds.
 * It allows setting ADC channel values for testing.
 */

#include <zephyr/shell/shell.h>
#include <stdlib.h>

/* Declare the injection function from adc_backend.c */
extern int adc_backend_inject_mv(unsigned int ch, int32_t mv);

static int cmd_adcset(const struct shell *sh, size_t argc, char **argv)
{
    unsigned int ch;
    int32_t mv;
    int ret;

    if (argc != 3) {
        shell_error(sh, "Usage: adcset <channel> <millivolts>");
        shell_error(sh, "  channel: 0-%d", CONFIG_APP_NUM_CH - 1);
        shell_error(sh, "  millivolts: 0-3300");
        return -EINVAL;
    }

    ch = (unsigned int)strtoul(argv[1], NULL, 10);
    mv = (int32_t)strtol(argv[2], NULL, 10);

    if (ch >= CONFIG_APP_NUM_CH) {
        shell_error(sh, "Invalid channel %u (max %d)", ch, CONFIG_APP_NUM_CH - 1);
        return -EINVAL;
    }

    ret = adc_backend_inject_mv(ch, mv);
    if (ret < 0) {
        shell_error(sh, "Injection failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Set ch[%u] = %d mV", ch, mv);
    shell_print(sh, "Next sample will reflect this value.");

    return 0;
}

SHELL_CMD_REGISTER(adcset, NULL,
    "Inject ADC value (SIM only)\n"
    "Usage: adcset <channel> <millivolts>",
    cmd_adcset);

