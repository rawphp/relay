#include "Unity/unity.h"
#include "proc_log_partial.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Test helpers ────────────────────────────────────────────────────── */

static char *tmplog_path = NULL;
static relay_log_t *tmplog = NULL;

static time_t fixed_now(void) { return 1708070400; }
static struct tm *fixed_localtime(const time_t *t, struct tm *r)
{
    (void)t;
    r->tm_year = 124; r->tm_mon = 1; r->tm_mday = 16;
    r->tm_hour = 12;  r->tm_min = 0; r->tm_sec  = 0;
    return r;
}
static relay_clock_t fixed_clock = { .now = fixed_now, .localtime_r = fixed_localtime };

static void create_tmplog(void)
{
    tmplog_path = strdup(TEST_TMP_DIR "/test_proc_log_partial_XXXXXX");
    int fd = mkstemp(tmplog_path);
    close(fd); /* log_create opens it again */
    tmplog = log_create(tmplog_path, LOG_INFO, &fixed_clock);
}

static char *read_tmplog(void)
{
    log_close(tmplog);
    tmplog = NULL;
    FILE *f = fopen(tmplog_path, "r");
    if (!f) return strdup("");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static void cleanup_tmplog(void)
{
    if (tmplog) { log_close(tmplog); tmplog = NULL; }
    if (tmplog_path) { unlink(tmplog_path); free(tmplog_path); tmplog_path = NULL; }
}

/* ── Test 1: NULL log — no crash ─────────────────────────────────────── */
static void test_null_log_no_crash(void)
{
    /* Must not crash when log is NULL */
    proc_log_partial(NULL, "some data", 9);
    TEST_PASS();
}

/* ── Test 2: NULL / empty buf — no crash ────────────────────────────── */
static void test_null_buf_no_crash(void)
{
    create_tmplog();
    proc_log_partial(tmplog, NULL, 0);
    proc_log_partial(tmplog, "x", 0);   /* len==0 also a no-op */
    char *out = read_tmplog();
    /* No warn line should appear */
    TEST_ASSERT_NULL(strstr(out, "[WARN]"));
    free(out);
    cleanup_tmplog();
}

/* ── Test 3: Short buffer is logged verbatim ─────────────────────────── */
static void test_short_buf_logged(void)
{
    create_tmplog();
    const char *msg = "Error: rate limit exceeded";
    proc_log_partial(tmplog, msg, strlen(msg));
    char *out = read_tmplog();
    TEST_ASSERT_NOT_NULL(strstr(out, "[claude] partial output:"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Error: rate limit exceeded"));
    free(out);
    cleanup_tmplog();
}

/* ── Test 4: Long buffer — only last PROC_LOG_PARTIAL_MAX bytes logged ── */
static void test_long_buf_truncated_to_tail(void)
{
    create_tmplog();

    /* Build a 1000-byte buffer: first 488 bytes 'A', last 512 bytes 'B' */
    char buf[1000];
    memset(buf,       'A', 1000 - PROC_LOG_PARTIAL_MAX);
    memset(buf + 1000 - PROC_LOG_PARTIAL_MAX, 'B', PROC_LOG_PARTIAL_MAX);

    proc_log_partial(tmplog, buf, sizeof(buf));
    char *out = read_tmplog();

    /* The logged slice must contain 'B' characters */
    TEST_ASSERT_NOT_NULL(strstr(out, "BBBB"));

    /* The 'A' fill must NOT appear — we only log the tail */
    TEST_ASSERT_NULL(strstr(out, "AAAA"));

    free(out);
    cleanup_tmplog();
}

/* ── Test 5: Exactly PROC_LOG_PARTIAL_MAX bytes — logged in full ──────── */
static void test_exact_max_buf_logged(void)
{
    create_tmplog();

    char buf[PROC_LOG_PARTIAL_MAX];
    memset(buf, 'Z', sizeof(buf));
    buf[PROC_LOG_PARTIAL_MAX - 1] = '\0'; /* make printable / null-term safe */

    proc_log_partial(tmplog, buf, sizeof(buf));
    char *out = read_tmplog();
    TEST_ASSERT_NOT_NULL(strstr(out, "ZZZZ"));
    free(out);
    cleanup_tmplog();
}

/* ── Suite ───────────────────────────────────────────────────────────── */
void test_proc_log_partial_suite(void)
{
    RUN_TEST(test_null_log_no_crash);
    RUN_TEST(test_null_buf_no_crash);
    RUN_TEST(test_short_buf_logged);
    RUN_TEST(test_long_buf_truncated_to_tail);
    RUN_TEST(test_exact_max_buf_logged);
}
