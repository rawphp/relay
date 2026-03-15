#include "Unity/unity.h"
#include "agent_bus.h"
#include "agent_bus_rate.h"
#include "relay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_sockpath[256];
static char g_logdir[256];

static void setup_sockpath(void)
{
    /* Use a predictable but unique temp socket path */
    snprintf(g_sockpath, sizeof(g_sockpath),
             TEST_TMP_DIR "/relay_test_agent_bus_%d.sock", (int)getpid());
    unlink(g_sockpath); /* remove stale */
}

static void setup_logdir(void)
{
    snprintf(g_logdir, sizeof(g_logdir), TEST_TMP_DIR "/relay_agbus_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_logdir));
}

static void teardown_logdir(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/agent-bus.jsonl", g_logdir);
    remove(path);
    rmdir(g_logdir);
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/* agent_bus_init binds a socket file at the given path. */
static void test_init_creates_socket(void)
{
    setup_sockpath();
    int rc = agent_bus_init(g_sockpath);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    /* The socket file must exist */
    TEST_ASSERT_EQUAL_INT(0, access(g_sockpath, F_OK));
    agent_bus_destroy();
}

/* agent_bus_get_fd returns a valid fd after init. */
static void test_get_fd_after_init(void)
{
    setup_sockpath();
    agent_bus_init(g_sockpath);
    TEST_ASSERT_TRUE(agent_bus_get_fd() >= 0);
    agent_bus_destroy();
}

/* accept_message on an idle socket returns RELAY_ERR_NOTFOUND (non-blocking). */
static void test_accept_no_client_returns_not_found(void)
{
    setup_sockpath();
    agent_bus_init(g_sockpath);

    agent_bus_message_t msg;
    int rc = agent_bus_accept_message(&msg);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_NOTFOUND, rc);

    agent_bus_destroy();
}

/* send → accept roundtrip: fields are preserved. */
static void test_send_and_receive_roundtrip(void)
{
    setup_sockpath();
    agent_bus_init(g_sockpath);

    int rc = agent_bus_send(g_sockpath, "nova", g_sockpath,
                            "Hey Henry, what's up?", 1, 1, "henry");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    agent_bus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    rc = agent_bus_accept_message(&msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    TEST_ASSERT_EQUAL_STRING("nova", msg.from);
    TEST_ASSERT_NOT_NULL(strstr(msg.text, "Hey Henry"));
    TEST_ASSERT_EQUAL_INT(1, msg.depth);
    TEST_ASSERT_EQUAL_INT(1, msg.is_autonomous);
    TEST_ASSERT_EQUAL_STRING("henry", msg.addressed_to);

    agent_bus_destroy();
}

/* agent_bus_log writes a JSONL entry to {log_dir}/agent-bus.jsonl. */
static void test_log_writes_jsonl(void)
{
    setup_logdir();

    agent_bus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.from, sizeof(msg.from), "human");
    snprintf(msg.text, sizeof(msg.text), "Hello from the test");
    snprintf(msg.msg_id, sizeof(msg.msg_id), "test-id-001");
    msg.ts    = 1740000000LL;
    msg.depth = 0;

    agent_bus_log(g_logdir, "in", &msg, "Hi back");

    char path[512];
    snprintf(path, sizeof(path), "%s/agent-bus.jsonl", g_logdir);
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));

    /* Check the content looks like JSONL */
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL(f);
    char line[4096];
    TEST_ASSERT_NOT_NULL(fgets(line, sizeof(line), f));
    fclose(f);

    TEST_ASSERT_NOT_NULL(strstr(line, "\"direction\""));
    TEST_ASSERT_NOT_NULL(strstr(line, "human"));
    TEST_ASSERT_NOT_NULL(strstr(line, "Hello from the test"));
    TEST_ASSERT_NOT_NULL(strstr(line, "Hi back"));

    teardown_logdir();
}

/* agent_bus_destroy removes the socket file. */
static void test_destroy_removes_socket(void)
{
    setup_sockpath();
    agent_bus_init(g_sockpath);
    TEST_ASSERT_EQUAL_INT(0, access(g_sockpath, F_OK)); /* exists */

    agent_bus_destroy();
    TEST_ASSERT_NOT_EQUAL(0, access(g_sockpath, F_OK)); /* gone */
}

/* ── Rate limiter tests ──────────────────────────────────────────────────── */

static void test_rate_limiter_allows_under_limit(void)
{
    bus_rate_limiter_t rl;
    bus_rate_init(&rl, 3);

    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
}

static void test_rate_limiter_blocks_over_limit(void)
{
    bus_rate_limiter_t rl;
    bus_rate_init(&rl, 3);

    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
    /* 4th in same second should be blocked */
    TEST_ASSERT_EQUAL_INT(0, bus_rate_allow(&rl, 100));
}

static void test_rate_limiter_resets_after_window(void)
{
    bus_rate_limiter_t rl;
    bus_rate_init(&rl, 2);

    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 100));
    TEST_ASSERT_EQUAL_INT(0, bus_rate_allow(&rl, 100)); /* blocked */

    /* One second later, should allow again */
    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 101));
    TEST_ASSERT_EQUAL_INT(1, bus_rate_allow(&rl, 101));
    TEST_ASSERT_EQUAL_INT(0, bus_rate_allow(&rl, 101)); /* blocked again */
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void test_agent_bus_suite(void)
{
    RUN_TEST(test_init_creates_socket);
    RUN_TEST(test_get_fd_after_init);
    RUN_TEST(test_accept_no_client_returns_not_found);
    RUN_TEST(test_send_and_receive_roundtrip);
    RUN_TEST(test_log_writes_jsonl);
    RUN_TEST(test_destroy_removes_socket);
    RUN_TEST(test_rate_limiter_allows_under_limit);
    RUN_TEST(test_rate_limiter_blocks_over_limit);
    RUN_TEST(test_rate_limiter_resets_after_window);
}
