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
        zassert_false(snapshot.valid[i], "ch[%d] has no reading after init", i);
    }
}

/**
 * @brief Test update and read
 */
ZTEST(regs, test_update_read)
{
    struct adc_regs snapshot;
    int32_t test_values[NUM_CH] = {1000, 2000, 3000, 4000};
    bool all_valid[NUM_CH];

    for (int i = 0; i < NUM_CH; i++) {
        all_valid[i] = true;
    }

    /* Update with test values */
    regs_update(test_values, all_valid);

    /* Read back */
    regs_read(&snapshot);

    /* Verify values */
    zassert_equal(snapshot.seq, 1, "seq should increment to 1");
    zassert_true(snapshot.last_sample_uptime_ms > 0, "timestamp should be set");

    for (int i = 0; i < NUM_CH; i++) {
        zassert_equal(snapshot.mv[i], test_values[i],
                      "ch[%d] should match input value", i);
        zassert_true(snapshot.valid[i], "ch[%d] should be valid", i);
    }
}

/**
 * @brief Test sequence number increment
 */
ZTEST(regs, test_seq_increment)
{
    struct adc_regs snapshot;
    int32_t values[NUM_CH] = {0};
    bool valid[NUM_CH] = {false};

    /* Update multiple times */
    for (uint32_t expected_seq = 1; expected_seq <= 5; expected_seq++) {
        regs_update(values, valid);
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
    bool valid[NUM_CH] = {false};

    regs_update(values1, valid);
    regs_read(&snapshot);
    zassert_equal(snapshot.mv[0], 100, "First update should set ch[0]=100");

    regs_update(values2, valid);
    regs_read(&snapshot);
    zassert_equal(snapshot.mv[0], 500, "Second update should overwrite ch[0]=500");
    zassert_equal(snapshot.seq, 2, "seq should be 2 after two updates");
}

/**
 * @brief An invalid channel is not the same as a real 0 mV
 *
 * The whole point of the flag: two channels can both hold 0 in mv[] while
 * only one of them is a reading.
 */
ZTEST(regs, test_invalid_is_not_zero)
{
    struct adc_regs snapshot;
    int32_t values[NUM_CH] = {0};
    bool valid[NUM_CH];

    for (int i = 0; i < NUM_CH; i++) {
        valid[i] = true;
    }
    valid[1] = false;

    regs_update(values, valid);
    regs_read(&snapshot);

    zassert_equal(snapshot.mv[0], 0, "ch[0] reads 0 mV");
    zassert_true(snapshot.valid[0], "ch[0] is a real 0 mV");
    zassert_equal(snapshot.mv[1], 0, "ch[1] also holds 0");
    zassert_false(snapshot.valid[1], "ch[1] is not a reading");
}

ZTEST_SUITE(regs, NULL, NULL, regs_before, NULL, NULL);

