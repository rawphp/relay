#include "Unity/unity.h"
#include "pending_bus_messages.h"
#include "agent_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_tmpdir[256];

static void setup_tmpdir(void)
{
    snprintf(g_tmpdir, sizeof(g_tmpdir), TEST_TMP_DIR "/relay_pending_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_tmpdir));
    /* create data/ sub-directory */
    char data[300];
    snprintf(data, sizeof(data), "%s/data", g_tmpdir);
    mkdir(data, 0755);
}

static void teardown_tmpdir(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/data/pending_bus_messages.jsonl", g_tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data", g_tmpdir);
    rmdir(path);
    rmdir(g_tmpdir);
}

/* Build a minimal agent_bus_message_t for testing. */
static agent_bus_message_t make_msg(const char *from, const char *text, long long ts)
{
    agent_bus_message_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.from, sizeof(m.from), "%s", from);
    snprintf(m.text, sizeof(m.text), "%s", text);
    m.ts = ts;
    return m;
}

/* Count lines in the pending file. */
static int count_lines(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/data/pending_bus_messages.jsonl", g_tmpdir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int n = 0;
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '\0' && line[0] != '\n') n++;
    }
    fclose(f);
    return n;
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

static void test_load_empty_when_no_file(void)
{
    char buf[1024] = "unchanged";
    int rc = pending_bus_load("/nonexistent/path/xyz", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, rc);
    /* buf should be cleared (empty string) */
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_save_creates_file(void)
{
    setup_tmpdir();
    agent_bus_message_t m = make_msg("human", "Nova, thoughts?", 1740556454);
    pending_bus_save(g_tmpdir, &m);

    char path[512];
    snprintf(path, sizeof(path), "%s/data/pending_bus_messages.jsonl", g_tmpdir);
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));
    teardown_tmpdir();
}

static void test_load_returns_one_after_save(void)
{
    setup_tmpdir();
    agent_bus_message_t m = make_msg("human", "Nova, thoughts?", 1740556454);
    pending_bus_save(g_tmpdir, &m);

    char buf[4096] = "";
    int rc = pending_bus_load(g_tmpdir, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Nova, thoughts?"));
    teardown_tmpdir();
}

static void test_load_formats_multiple_messages(void)
{
    setup_tmpdir();
    agent_bus_message_t m1 = make_msg("human", "first message", 1740556400);
    agent_bus_message_t m2 = make_msg("human", "second message", 1740556460);
    pending_bus_save(g_tmpdir, &m1);
    pending_bus_save(g_tmpdir, &m2);

    char buf[4096] = "";
    int rc = pending_bus_load(g_tmpdir, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(1, rc);
    /* Both messages must appear, in order */
    char *p1 = strstr(buf, "first message");
    char *p2 = strstr(buf, "second message");
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_TRUE(p1 < p2); /* first appears before second */
    teardown_tmpdir();
}

static void test_clear_removes_file(void)
{
    setup_tmpdir();
    agent_bus_message_t m = make_msg("human", "hello", 1740556454);
    pending_bus_save(g_tmpdir, &m);

    pending_bus_clear(g_tmpdir);

    char buf[1024] = "";
    int rc = pending_bus_load(g_tmpdir, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, rc);
    teardown_tmpdir();
}

static void test_save_caps_at_20_entries(void)
{
    setup_tmpdir();
    for (int i = 0; i < 25; i++) {
        char text[64];
        snprintf(text, sizeof(text), "message %d", i);
        agent_bus_message_t m = make_msg("human", text, 1740556400 + i);
        pending_bus_save(g_tmpdir, &m);
    }
    int lines = count_lines();
    TEST_ASSERT_TRUE(lines <= 20);
    teardown_tmpdir();
}

static void test_load_handles_corrupt_jsonl_line(void)
{
    setup_tmpdir();
    /* Write one valid line and one garbage line directly */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/pending_bus_messages.jsonl", g_tmpdir);
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "{\"from\":\"human\",\"ts\":1740556454,\"text\":\"valid message\"}\n");
    fprintf(f, "NOT JSON AT ALL @@@@\n");
    fclose(f);

    char buf[4096] = "";
    int rc = pending_bus_load(g_tmpdir, buf, sizeof(buf));
    /* Should not crash; valid message should appear */
    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_NOT_NULL(strstr(buf, "valid message"));
    teardown_tmpdir();
}

static void test_clear_on_empty_dir_is_safe(void)
{
    setup_tmpdir();
    /* No file written yet — clear should not crash */
    pending_bus_clear(g_tmpdir);
    teardown_tmpdir();
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void pending_bus_messages_suite(void)
{
    RUN_TEST(test_load_empty_when_no_file);
    RUN_TEST(test_save_creates_file);
    RUN_TEST(test_load_returns_one_after_save);
    RUN_TEST(test_load_formats_multiple_messages);
    RUN_TEST(test_clear_removes_file);
    RUN_TEST(test_save_caps_at_20_entries);
    RUN_TEST(test_load_handles_corrupt_jsonl_line);
    RUN_TEST(test_clear_on_empty_dir_is_safe);
}
