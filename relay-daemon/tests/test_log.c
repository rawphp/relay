#include "Unity/unity.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ── Mock clock ─────────────────────────────────────────────────────── */

static time_t mock_time = 1708070400; /* 2024-02-16 12:00:00 UTC */

static time_t mock_now(void)
{
    return mock_time;
}

static struct tm *mock_localtime_r(const time_t *t, struct tm *result)
{
    /* Return a fixed time for predictable output */
    (void)t;
    result->tm_year = 124; /* 2024 */
    result->tm_mon  = 1;   /* Feb */
    result->tm_mday = 16;
    result->tm_hour = 12;
    result->tm_min  = 0;
    result->tm_sec  = 0;
    return result;
}

static relay_clock_t mock_clock = {
    .now = mock_now,
    .localtime_r = mock_localtime_r
};

/* ── Test: Log format produces correct timestamp and level ──────────── */
static void test_log_format_info(void)
{
    char buf[256];
    struct tm tm = {0};
    tm.tm_year = 124;
    tm.tm_mon  = 1;
    tm.tm_mday = 16;
    tm.tm_hour = 12;
    tm.tm_min  = 30;
    tm.tm_sec  = 45;

    int n = log_format(buf, sizeof(buf), LOG_INFO, &tm, "hello world");
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "[2024-02-16 12:30:45]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[INFO]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "hello world"));
}

static void test_log_format_warn(void)
{
    char buf[256];
    struct tm tm = {0};
    tm.tm_year = 124;
    tm.tm_mon  = 0;
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;

    int n = log_format(buf, sizeof(buf), LOG_WARN, &tm, "warning msg");
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "[WARN]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "warning msg"));
}

static void test_log_format_error(void)
{
    char buf[256];
    struct tm tm = {0};
    tm.tm_year = 124;
    tm.tm_mon  = 11;
    tm.tm_mday = 31;
    tm.tm_hour = 23;
    tm.tm_min  = 59;
    tm.tm_sec  = 59;

    int n = log_format(buf, sizeof(buf), LOG_ERROR, &tm, "error msg");
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "[ERROR]"));
}

/* ── Test: Token masking ────────────────────────────────────────────── */
static void test_log_mask_long_token(void)
{
    const char *masked = log_mask("sk-ant-api123456789abcdef");
    TEST_ASSERT_EQUAL_STRING("sk-ant...", masked);
}

static void test_log_mask_short_value(void)
{
    const char *masked = log_mask("abc");
    /* Short values shown as-is (not sensitive enough to mask) */
    TEST_ASSERT_EQUAL_STRING("abc", masked);
}

static void test_log_mask_null(void)
{
    const char *masked = log_mask(NULL);
    TEST_ASSERT_EQUAL_STRING("(null)", masked);
}

static void test_log_mask_empty(void)
{
    const char *masked = log_mask("");
    TEST_ASSERT_EQUAL_STRING("", masked);
}

static void test_log_mask_bot_token(void)
{
    /* A Telegram bot token is long enough to be masked */
    const char *masked = log_mask("7654321:AAF-xyzBot_Token_Here_1234567890");
    /* Only first 6 chars shown */
    TEST_ASSERT_EQUAL_STRING("765432...", masked);
}

/* ── Test: Log writes to file ───────────────────────────────────────── */
static void test_log_writes_to_file(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_log.txt";
    unlink(path);

    relay_log_t *log = log_create(path, LOG_INFO, &mock_clock);
    TEST_ASSERT_NOT_NULL(log);

    log_write(log, LOG_INFO, "test message %d", 42);
    log_close(log);

    /* Read the file back */
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL(f);

    char buf[512];
    char *line = fgets(buf, sizeof(buf), f);
    fclose(f);
    unlink(path);

    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_NOT_NULL(strstr(buf, "test message 42"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[INFO]"));
}

/* ── Test: Level filtering ──────────────────────────────────────────── */
static void test_log_level_filtering(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_log_level.txt";
    unlink(path);

    /* Only WARN and above */
    relay_log_t *log = log_create(path, LOG_WARN, &mock_clock);
    TEST_ASSERT_NOT_NULL(log);

    log_write(log, LOG_INFO, "should be filtered");
    log_write(log, LOG_WARN, "should appear");
    log_write(log, LOG_ERROR, "should also appear");
    log_close(log);

    /* Read back and verify */
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL(f);

    char buf[1024];
    size_t read = fread(buf, 1, sizeof(buf) - 1, f);
    buf[read] = '\0';
    fclose(f);
    unlink(path);

    TEST_ASSERT_NULL(strstr(buf, "should be filtered"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "should appear"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "should also appear"));
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_log_suite(void)
{
    RUN_TEST(test_log_format_info);
    RUN_TEST(test_log_format_warn);
    RUN_TEST(test_log_format_error);
    RUN_TEST(test_log_mask_long_token);
    RUN_TEST(test_log_mask_short_value);
    RUN_TEST(test_log_mask_null);
    RUN_TEST(test_log_mask_empty);
    RUN_TEST(test_log_mask_bot_token);
    RUN_TEST(test_log_writes_to_file);
    RUN_TEST(test_log_level_filtering);
}
