/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for register file (regs.c)
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "regs.h"

/* Test fixture - runs before each test */
static void regs_before(void *fixture)
{
    ARG_UNUSED(fixture);
    regs_init();
}

/**
 * @brief Test initialization
 */
ZTEST(regs, test_init)
{
    struct adc_regs snapshot;

    regs_read(&snapshot);

    /* After init, all values should be zero */
    zassert_equal(snapshot.seq, 0, "seq should be 0 after init");
    zassert_equal(snapshot.last_sample_uptime_ms, 0, "timestamp should be 0 after init");

    for (int i = 0; i < NUM_CH; i++) {
        zassert_equal(snapshot.mv[i], 0, "ch[%d] should be 0 after init", i);
    }
}

/**
 * @brief Test update and read
 */
ZTEST(regs, test_update_read)
{
    struct adc_regs snapshot;
    int32_t test_values[NUM_CH] = {1000, 2000, 3000, 4000};

    /* Update with test values */
    regs_update(test_values);

    /* Read back */
    regs_read(&snapshot);

    /* Verify values */
    zassert_equal(snapshot.seq, 1, "seq should increment to 1");
    zassert_true(snapshot.last_sample_uptime_ms > 0, "timestamp should be set");

    for (int i = 0; i < NUM_CH; i++) {
        zassert_equal(snapshot.mv[i], test_values[i],
                      "ch[%d] should match input value", i);
    }
}

/**
 * @brief Test sequence number increment
 */
ZTEST(regs, test_seq_increment)
{
    struct adc_regs snapshot;
    int32_t values[NUM_CH] = {0};

    /* Update multiple times */
    for (uint32_t expected_seq = 1; expected_seq <= 5; expected_seq++) {
        regs_update(values);
        regs_read(&snapshot);
        zassert_equal(snapshot.seq, expected_seq,
                      "seq should be %u after %u updates", expected_seq, expected_seq);
    }
}

/**
 * @brief Test multiple updates
 */
ZTEST(regs, test_multiple_updates)
{
    struct adc_regs snapshot;
    int32_t values1[NUM_CH] = {100, 200, 300, 400};
    int32_t values2[NUM_CH] = {500, 600, 700, 800};

    regs_update(values1);
    regs_read(&snapshot);
    zassert_equal(snapshot.mv[0], 100, "First update should set ch[0]=100");

    regs_update(values2);
    regs_read(&snapshot);
    zassert_equal(snapshot.mv[0], 500, "Second update should overwrite ch[0]=500");
    zassert_equal(snapshot.seq, 2, "seq should be 2 after two updates");
}

ZTEST_SUITE(regs, NULL, NULL, regs_before, NULL, NULL);

