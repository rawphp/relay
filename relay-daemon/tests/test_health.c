#include "Unity/unity.h"
#include "health.h"

/* ── Test: Initial state is zero failures ───────────────────────────── */
static void test_health_initial_state(void)
{
    health_t *h = health_create(5);
    TEST_ASSERT_NOT_NULL(h);

    TEST_ASSERT_EQUAL_INT(0, health_failures(h, HEALTH_TELEGRAM));
    TEST_ASSERT_EQUAL_INT(0, health_failures(h, HEALTH_CLAUDE));
    TEST_ASSERT_EQUAL_INT(0, health_failures(h, HEALTH_HEARTBEAT));
    TEST_ASSERT_EQUAL_INT(0, health_is_persistent(h, HEALTH_TELEGRAM));

    health_free(h);
}

/* ── Test: Failures increment ───────────────────────────────────────── */
static void test_health_failures_increment(void)
{
    health_t *h = health_create(5);

    health_failure(h, HEALTH_TELEGRAM);
    TEST_ASSERT_EQUAL_INT(1, health_failures(h, HEALTH_TELEGRAM));

    health_failure(h, HEALTH_TELEGRAM);
    health_failure(h, HEALTH_TELEGRAM);
    TEST_ASSERT_EQUAL_INT(3, health_failures(h, HEALTH_TELEGRAM));

    /* Other components unaffected */
    TEST_ASSERT_EQUAL_INT(0, health_failures(h, HEALTH_CLAUDE));

    health_free(h);
}

/* ── Test: Success resets counter ───────────────────────────────────── */
static void test_health_success_resets(void)
{
    health_t *h = health_create(5);

    health_failure(h, HEALTH_CLAUDE);
    health_failure(h, HEALTH_CLAUDE);
    health_failure(h, HEALTH_CLAUDE);
    TEST_ASSERT_EQUAL_INT(3, health_failures(h, HEALTH_CLAUDE));

    health_success(h, HEALTH_CLAUDE);
    TEST_ASSERT_EQUAL_INT(0, health_failures(h, HEALTH_CLAUDE));

    health_free(h);
}

/* ── Test: Persistent failure detection ─────────────────────────────── */
static void test_health_persistent(void)
{
    health_t *h = health_create(3);

    health_failure(h, HEALTH_HEARTBEAT);
    health_failure(h, HEALTH_HEARTBEAT);
    TEST_ASSERT_EQUAL_INT(0, health_is_persistent(h, HEALTH_HEARTBEAT));

    health_failure(h, HEALTH_HEARTBEAT);
    TEST_ASSERT_EQUAL_INT(1, health_is_persistent(h, HEALTH_HEARTBEAT));

    /* Resets after success */
    health_success(h, HEALTH_HEARTBEAT);
    TEST_ASSERT_EQUAL_INT(0, health_is_persistent(h, HEALTH_HEARTBEAT));

    health_free(h);
}

/* ── Test: Exponential backoff ──────────────────────────────────────── */
static void test_health_backoff(void)
{
    health_t *h = health_create(5);

    /* 0 failures = 1 second */
    TEST_ASSERT_EQUAL_INT(1, health_backoff(h, HEALTH_TELEGRAM));

    health_failure(h, HEALTH_TELEGRAM); /* 1 failure = 2s */
    TEST_ASSERT_EQUAL_INT(2, health_backoff(h, HEALTH_TELEGRAM));

    health_failure(h, HEALTH_TELEGRAM); /* 2 failures = 4s */
    TEST_ASSERT_EQUAL_INT(4, health_backoff(h, HEALTH_TELEGRAM));

    health_failure(h, HEALTH_TELEGRAM); /* 3 failures = 8s */
    TEST_ASSERT_EQUAL_INT(8, health_backoff(h, HEALTH_TELEGRAM));

    health_free(h);
}

/* ── Test: Backoff capped at 300 seconds ────────────────────────────── */
static void test_health_backoff_cap(void)
{
    health_t *h = health_create(5);

    /* Push failures high */
    for (int i = 0; i < 20; i++) {
        health_failure(h, HEALTH_CLAUDE);
    }

    TEST_ASSERT_LESS_OR_EQUAL(300, health_backoff(h, HEALTH_CLAUDE));

    health_free(h);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_health_suite(void)
{
    RUN_TEST(test_health_initial_state);
    RUN_TEST(test_health_failures_increment);
    RUN_TEST(test_health_success_resets);
    RUN_TEST(test_health_persistent);
    RUN_TEST(test_health_backoff);
    RUN_TEST(test_health_backoff_cap);
}
