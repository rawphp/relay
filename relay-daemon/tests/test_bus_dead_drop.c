#include "Unity/unity.h"
#include "bus_dead_drop.h"
#include "agent_bus.h"
#include "relay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_addir[256];

static void setup_addir(void)
{
    snprintf(g_addir, sizeof(g_addir),
             TEST_TMP_DIR "/dead_drop_%d", (int)getpid());
    mkdir(g_addir, 0700);
}

static void cleanup_addir(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_addir);
    int __attribute__((unused)) rc = system(cmd);
}

static agent_bus_message_t make_msg(const char *from, const char *text)
{
    agent_bus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.from, sizeof(msg.from), "%s", from);
    snprintf(msg.text, sizeof(msg.text), "%s", text);
    msg.ts = 1710500000LL;
    snprintf(msg.msg_id, sizeof(msg.msg_id), "test-msg-001");
    return msg;
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/* Save creates the inbox directory and writes the message */
static void test_dead_drop_save(void)
{
    setup_addir();

    agent_bus_message_t msg = make_msg("kai", "Hey Ash, checking in.");
    int rc = bus_dead_drop_save(g_addir, "ash", &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* File should exist */
    char path[512];
    snprintf(path, sizeof(path), "%s/inbox/ash/pending.jsonl", g_addir);
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));

    cleanup_addir();
}

/* Load returns pending messages formatted as catch-up block */
static void test_dead_drop_load(void)
{
    setup_addir();

    agent_bus_message_t msg = make_msg("kai", "Hey Ash!");
    bus_dead_drop_save(g_addir, "ash", &msg);

    char out[4096];
    int count = bus_dead_drop_load(g_addir, "ash", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_NOT_NULL(strstr(out, "kai"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Hey Ash!"));

    cleanup_addir();
}

/* Load with empty inbox returns 0 */
static void test_dead_drop_load_empty(void)
{
    setup_addir();

    char out[4096];
    int count = bus_dead_drop_load(g_addir, "ash", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, count);

    cleanup_addir();
}

/* Multiple messages are all loaded */
static void test_dead_drop_multiple_messages(void)
{
    setup_addir();

    agent_bus_message_t msg1 = make_msg("kai", "First message");
    agent_bus_message_t msg2 = make_msg("nova", "Second message");
    bus_dead_drop_save(g_addir, "ash", &msg1);
    bus_dead_drop_save(g_addir, "ash", &msg2);

    char out[8192];
    int count = bus_dead_drop_load(g_addir, "ash", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_NOT_NULL(strstr(out, "First message"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Second message"));

    cleanup_addir();
}

/* Clear removes the pending file */
static void test_dead_drop_clear(void)
{
    setup_addir();

    agent_bus_message_t msg = make_msg("kai", "Hello");
    bus_dead_drop_save(g_addir, "ash", &msg);

    int rc = bus_dead_drop_clear(g_addir, "ash");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* File should be gone */
    char path[512];
    snprintf(path, sizeof(path), "%s/inbox/ash/pending.jsonl", g_addir);
    TEST_ASSERT_NOT_EQUAL(0, access(path, F_OK));

    /* Load should return 0 */
    char out[4096];
    TEST_ASSERT_EQUAL_INT(0, bus_dead_drop_load(g_addir, "ash", out, sizeof(out)));

    cleanup_addir();
}

/* Clear on nonexistent inbox returns RELAY_ERR_NOTFOUND */
static void test_dead_drop_clear_empty(void)
{
    setup_addir();
    int rc = bus_dead_drop_clear(g_addir, "nobody");
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_NOTFOUND, rc);
    cleanup_addir();
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void test_bus_dead_drop_suite(void)
{
    RUN_TEST(test_dead_drop_save);
    RUN_TEST(test_dead_drop_load);
    RUN_TEST(test_dead_drop_load_empty);
    RUN_TEST(test_dead_drop_multiple_messages);
    RUN_TEST(test_dead_drop_clear);
    RUN_TEST(test_dead_drop_clear_empty);
}
