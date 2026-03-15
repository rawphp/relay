#include "Unity/unity.h"
#include "telegram_offset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_tmpdir[256];

static void setup_tmpdir(void)
{
    snprintf(g_tmpdir, sizeof(g_tmpdir), TEST_TMP_DIR "/relay_offset_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_tmpdir));
    char data[300];
    snprintf(data, sizeof(data), "%s/data", g_tmpdir);
    mkdir(data, 0755);
    snprintf(data, sizeof(data), "%s/data/state", g_tmpdir);
    mkdir(data, 0755);
}

static void teardown_tmpdir(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/data/state/telegram-offset.txt", g_tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data/state", g_tmpdir);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/data", g_tmpdir);
    rmdir(path);
    rmdir(g_tmpdir);
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

static void test_load_returns_zero_when_no_file(void)
{
    setup_tmpdir();
    long long offset = telegram_offset_load(g_tmpdir);
    TEST_ASSERT_EQUAL_INT64(0, offset);
    teardown_tmpdir();
}

static void test_save_creates_file(void)
{
    setup_tmpdir();
    int rc = telegram_offset_save(g_tmpdir, 42LL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    char path[512];
    snprintf(path, sizeof(path), "%s/data/state/telegram-offset.txt", g_tmpdir);
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));
    teardown_tmpdir();
}

static void test_load_round_trips_offset(void)
{
    setup_tmpdir();
    telegram_offset_save(g_tmpdir, 12345LL);
    long long offset = telegram_offset_load(g_tmpdir);
    TEST_ASSERT_EQUAL_INT64(12345LL, offset);
    teardown_tmpdir();
}

static void test_save_overwrites_previous(void)
{
    setup_tmpdir();
    telegram_offset_save(g_tmpdir, 100LL);
    telegram_offset_save(g_tmpdir, 200LL);
    long long offset = telegram_offset_load(g_tmpdir);
    TEST_ASSERT_EQUAL_INT64(200LL, offset);
    teardown_tmpdir();
}

static void test_save_large_offset(void)
{
    setup_tmpdir();
    telegram_offset_save(g_tmpdir, 9999999999LL);
    long long offset = telegram_offset_load(g_tmpdir);
    TEST_ASSERT_EQUAL_INT64(9999999999LL, offset);
    teardown_tmpdir();
}

static void test_save_handles_null_workspace(void)
{
    /* Should not crash */
    int rc = telegram_offset_save(NULL, 42LL);
    TEST_ASSERT_NOT_EQUAL(0, rc);
}

static void test_load_handles_null_workspace(void)
{
    /* Should not crash, returns 0 */
    long long offset = telegram_offset_load(NULL);
    TEST_ASSERT_EQUAL_INT64(0, offset);
}

/* ── Suite ────────────────────────────────────────────────────────────────── */

void test_telegram_offset_suite(void)
{
    RUN_TEST(test_load_returns_zero_when_no_file);
    RUN_TEST(test_save_creates_file);
    RUN_TEST(test_load_round_trips_offset);
    RUN_TEST(test_save_overwrites_previous);
    RUN_TEST(test_save_large_offset);
    RUN_TEST(test_save_handles_null_workspace);
    RUN_TEST(test_load_handles_null_workspace);
}
