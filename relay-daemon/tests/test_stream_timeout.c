#include "Unity/unity.h"
#include "stream_timeout.h"
#include <limits.h>

/* ── compute_select_timeout tests ──────────────────────────────────── */

/* Wall-clock deadline is the binding constraint */
static void test_wall_deadline_tighter(void)
{
    time_t now       = 1000;
    time_t deadline  = 1005;  /* 5 s until deadline */
    time_t last_data = 980;   /* 20 s of idle already; 10 s idle left */
    int    idle_sec  = 30;

    /* wall: 5 s, idle remaining: 30-20=10 s → wall is tighter → 5 */
    TEST_ASSERT_EQUAL_INT(5, compute_select_timeout(deadline, last_data, idle_sec, now));
}

/* Idle window is the binding constraint */
static void test_idle_tighter(void)
{
    time_t now       = 1000;
    time_t deadline  = 1060;  /* 60 s until deadline */
    time_t last_data = 975;   /* 25 s idle; 5 s idle left */
    int    idle_sec  = 30;

    /* wall: 60 s, idle remaining: 30-25=5 s → idle is tighter → 5 */
    TEST_ASSERT_EQUAL_INT(5, compute_select_timeout(deadline, last_data, idle_sec, now));
}

/* Wall-clock already expired → returns 0 */
static void test_wall_already_expired(void)
{
    time_t now       = 1100;
    time_t deadline  = 1000; /* already past */
    time_t last_data = 1099;
    int    idle_sec  = 30;

    TEST_ASSERT_EQUAL_INT(0, compute_select_timeout(deadline, last_data, idle_sec, now));
}

/* Idle already expired → returns 0 */
static void test_idle_already_expired(void)
{
    time_t now       = 1000;
    time_t deadline  = 2000;
    time_t last_data = 960;  /* 40 s ago — past 30 s idle limit */
    int    idle_sec  = 30;

    TEST_ASSERT_EQUAL_INT(0, compute_select_timeout(deadline, last_data, idle_sec, now));
}

/* Both fresh → returns minimum of the two positive values */
static void test_both_fresh_returns_min(void)
{
    time_t now       = 1000;
    time_t deadline  = 1100; /* 100 s left */
    time_t last_data = 990;  /* 10 s idle used; 20 s idle left */
    int    idle_sec  = 30;

    /* min(100, 20) = 20 */
    TEST_ASSERT_EQUAL_INT(20, compute_select_timeout(deadline, last_data, idle_sec, now));
}

/* ── stream_idle_expired tests ─────────────────────────────────────── */

static void test_idle_not_expired(void)
{
    TEST_ASSERT_EQUAL_INT(0, stream_idle_expired(1000, 30, 1020));
}

static void test_idle_exactly_at_boundary(void)
{
    /* At exactly idle_sec elapsed, it is expired */
    TEST_ASSERT_EQUAL_INT(1, stream_idle_expired(1000, 30, 1030));
}

static void test_idle_well_past(void)
{
    TEST_ASSERT_EQUAL_INT(1, stream_idle_expired(1000, 30, 2000));
}

/* Global idle timeout must be ≥90s so long tool calls (e.g. make test)
 * that pause 60–90s between output tokens are not killed prematurely. */
static void test_global_idle_timeout_sufficient_for_tool_calls(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(90, RELAY_STREAM_IDLE_TIMEOUT_SEC);
}

/* ── Suite ─────────────────────────────────────────────────────────── */
void test_stream_timeout_suite(void)
{
    RUN_TEST(test_wall_deadline_tighter);
    RUN_TEST(test_idle_tighter);
    RUN_TEST(test_wall_already_expired);
    RUN_TEST(test_idle_already_expired);
    RUN_TEST(test_both_fresh_returns_min);
    RUN_TEST(test_idle_not_expired);
    RUN_TEST(test_global_idle_timeout_sufficient_for_tool_calls);
    RUN_TEST(test_idle_exactly_at_boundary);
    RUN_TEST(test_idle_well_past);
}
