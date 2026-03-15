#include "Unity/unity.h"
#include "health.h"
#include "el_reply.h"
#include <string.h>

/* ── Tests: health_is_persistent used for alerting ─────────────────── */

static void test_alert_fires_at_threshold(void)
{
    health_t *h = health_create(3);
    TEST_ASSERT_NOT_NULL(h);

    /* Below threshold: no alert */
    health_failure(h, HEALTH_LLM);
    health_failure(h, HEALTH_LLM);
    TEST_ASSERT_EQUAL_INT(0, health_is_persistent(h, HEALTH_LLM));

    /* At threshold: alert */
    health_failure(h, HEALTH_LLM);
    TEST_ASSERT_EQUAL_INT(1, health_is_persistent(h, HEALTH_LLM));

    health_free(h);
}

static void test_alert_resets_on_success(void)
{
    health_t *h = health_create(3);
    TEST_ASSERT_NOT_NULL(h);

    /* Push past threshold */
    for (int i = 0; i < 4; i++) {
        health_failure(h, HEALTH_LLM);
    }
    TEST_ASSERT_EQUAL_INT(1, health_is_persistent(h, HEALTH_LLM));

    /* Success resets */
    health_success(h, HEALTH_LLM);
    TEST_ASSERT_EQUAL_INT(0, health_is_persistent(h, HEALTH_LLM));

    /* Can fire again after re-accumulating failures */
    for (int i = 0; i < 3; i++) {
        health_failure(h, HEALTH_LLM);
    }
    TEST_ASSERT_EQUAL_INT(1, health_is_persistent(h, HEALTH_LLM));

    health_free(h);
}

static void test_health_alert_text_format(void)
{
    char buf[512];
    el_health_alert_text(5, "CLAUDECODE nesting error", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(buf);
    /* Must mention failure count */
    TEST_ASSERT_NOT_NULL(strstr(buf, "5"));
    /* Must mention error detail */
    TEST_ASSERT_NOT_NULL(strstr(buf, "CLAUDECODE"));
    /* Must suggest checking logs */
    TEST_ASSERT_NOT_NULL(strstr(buf, "logs"));
}

static void test_health_alert_text_null_detail(void)
{
    char buf[512];
    el_health_alert_text(3, NULL, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "3"));
    /* Must not crash with NULL detail */
    TEST_ASSERT_TRUE(strlen(buf) > 0);
}

static void test_health_alert_text_empty_detail(void)
{
    char buf[512];
    el_health_alert_text(3, "", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_TRUE(strlen(buf) > 0);
}

void test_health_alert_suite(void)
{
    RUN_TEST(test_alert_fires_at_threshold);
    RUN_TEST(test_alert_resets_on_success);
    RUN_TEST(test_health_alert_text_format);
    RUN_TEST(test_health_alert_text_null_detail);
    RUN_TEST(test_health_alert_text_empty_detail);
}
