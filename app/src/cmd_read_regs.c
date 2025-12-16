/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shell command: adcregs - Print ADC register file contents
 */

#include <zephyr/shell/shell.h>
#include "regs.h"

static int cmd_adcregs(const struct shell *sh, size_t argc, char **argv)
{
    struct adc_regs snapshot;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    regs_read(&snapshot);

    shell_print(sh, "ADC Register File:");
    shell_print(sh, "  seq:       %u", snapshot.seq);
    shell_print(sh, "  timestamp: %lld ms", snapshot.last_sample_uptime_ms);
    shell_print(sh, "  channels:");

    for (int i = 0; i < NUM_CH; i++) {
        shell_print(sh, "    ch[%d]: %d mV", i, snapshot.mv[i]);
    }

    return 0;
}

SHELL_CMD_REGISTER(adcregs, NULL, "Print ADC register file contents", cmd_adcregs);

