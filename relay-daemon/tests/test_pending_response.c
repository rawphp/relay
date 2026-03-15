#include "Unity/unity.h"
#include "pending_response.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_tmpdir[256];

static void setup_tmpdir(void)
{
    snprintf(g_tmpdir, sizeof(g_tmpdir), TEST_TMP_DIR "/relay_pr_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_tmpdir));
    /* create data/state sub-directory */
    char data[300];
    snprintf(data, sizeof(data), "%s/data", g_tmpdir);
    mkdir(data, 0755);
    snprintf(data, sizeof(data), "%s/data/state", g_tmpdir);
    mkdir(data, 0755);
}

static void teardown_tmpdir(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/data/state/pending-response.json", g_tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data/state", g_tmpdir);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/data", g_tmpdir);
    rmdir(path);
    rmdir(g_tmpdir);
}

static int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

static void test_load_returns_0_when_no_file(void)
{
    setup_tmpdir();
    char chat_id[64] = "";
    char text[256] = "";
    int rc = pending_response_load(g_tmpdir, chat_id, sizeof(chat_id),
                                   text, sizeof(text));
    TEST_ASSERT_EQUAL_INT(0, rc);
    teardown_tmpdir();
}

static void test_write_creates_file(void)
{
    setup_tmpdir();
    pending_response_write(g_tmpdir, "123456", "Hello world");
    char path[512];
    snprintf(path, sizeof(path), "%s/data/state/pending-response.json", g_tmpdir);
    TEST_ASSERT_TRUE(file_exists(path));
    teardown_tmpdir();
}

static void test_load_returns_1_after_write(void)
{
    setup_tmpdir();
    pending_response_write(g_tmpdir, "123456", "Hello world");
    char chat_id[64] = "";
    char text[256] = "";
    int rc = pending_response_load(g_tmpdir, chat_id, sizeof(chat_id),
                                   text, sizeof(text));
    TEST_ASSERT_EQUAL_INT(1, rc);
    teardown_tmpdir();
}

static void test_load_round_trips_chat_id(void)
{
    setup_tmpdir();
    pending_response_write(g_tmpdir, "987654321", "Some message");
    char chat_id[64] = "";
    char text[256] = "";
    pending_response_load(g_tmpdir, chat_id, sizeof(chat_id),
                          text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("987654321", chat_id);
    teardown_tmpdir();
}

static void test_load_round_trips_text(void)
{
    setup_tmpdir();
    pending_response_write(g_tmpdir, "111", "What is the meaning of life?");
    char chat_id[64] = "";
    char text[256] = "";
    pending_response_load(g_tmpdir, chat_id, sizeof(chat_id),
                          text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("What is the meaning of life?", text);
    teardown_tmpdir();
}

static void test_delete_removes_file(void)
{
    setup_tmpdir();
    pending_response_write(g_tmpdir, "111", "test");
    pending_response_delete(g_tmpdir);
    char path[512];
    snprintf(path, sizeof(path), "%s/data/state/pending-response.json", g_tmpdir);
    TEST_ASSERT_FALSE(file_exists(path));
    teardown_tmpdir();
}

static void test_load_returns_0_after_delete(void)
{
    setup_tmpdir();
    pending_response_write(g_tmpdir, "111", "test");
    pending_response_delete(g_tmpdir);
    char chat_id[64] = "";
    char text[256] = "";
    int rc = pending_response_load(g_tmpdir, chat_id, sizeof(chat_id),
                                   text, sizeof(text));
    TEST_ASSERT_EQUAL_INT(0, rc);
    teardown_tmpdir();
}

static void test_write_handles_null_workspace(void)
{
    /* Should not crash */
    pending_response_write(NULL, "111", "test");
}

static void test_delete_handles_no_file(void)
{
    setup_tmpdir();
    /* Should not crash when file doesn't exist */
    pending_response_delete(g_tmpdir);
    teardown_tmpdir();
}

/* ── Suite ────────────────────────────────────────────────────────────────── */

void test_pending_response_suite(void)
{
    RUN_TEST(test_load_returns_0_when_no_file);
    RUN_TEST(test_write_creates_file);
    RUN_TEST(test_load_returns_1_after_write);
    RUN_TEST(test_load_round_trips_chat_id);
    RUN_TEST(test_load_round_trips_text);
    RUN_TEST(test_delete_removes_file);
    RUN_TEST(test_load_returns_0_after_delete);
    RUN_TEST(test_write_handles_null_workspace);
    RUN_TEST(test_delete_handles_no_file);
}
