#include "Unity/unity.h"
#include "bus_directive.h"
#include "peer_registry.h"
#include "relay.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_regfile[256];

static void write_reg(const char *content)
{
    snprintf(g_regfile, sizeof(g_regfile),
             TEST_TMP_DIR "/test_bus_dir_%d.txt", (int)getpid());
    FILE *f = fopen(g_regfile, "w");
    if (f) { fputs(content, f); fclose(f); }
}

static void cleanup(void)
{
    peer_registry_destroy();
    if (g_regfile[0]) { unlink(g_regfile); g_regfile[0] = '\0'; }
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/* No directives — text passes through unchanged */
static void test_directive_no_directives(void)
{
    write_reg("ash=/tmp/ash\n");
    peer_registry_init(g_regfile, "kai");

    char out[1024];
    int count = bus_directive_process(
        "Hello, how are you?", out, sizeof(out), "kai", "/tmp/kai.sock");
    TEST_ASSERT_EQUAL_INT(0, count);
    TEST_ASSERT_EQUAL_STRING("Hello, how are you?", out);

    cleanup();
}

/* Single directive is stripped and note appended */
static void test_directive_single_send(void)
{
    write_reg("ash=/tmp/ash\n");
    peer_registry_init(g_regfile, "kai");

    char out[1024];
    int count = bus_directive_process(
        "Sure!\n[AGENT_BUS_SEND to=ash] Hey Ash, how are you?\nDone.",
        out, sizeof(out), "kai", "/tmp/kai.sock");

    TEST_ASSERT_EQUAL_INT(1, count);
    /* Directive line should be stripped */
    TEST_ASSERT_NULL(strstr(out, "AGENT_BUS_SEND"));
    /* Status note should be present */
    TEST_ASSERT_NOT_NULL(strstr(out, "ash"));

    cleanup();
}

/* Multiple directives are all processed */
static void test_directive_multiple_sends(void)
{
    write_reg("ash=/tmp/ash\nnova=/tmp/nova\n");
    peer_registry_init(g_regfile, "kai");

    char out[2048];
    int count = bus_directive_process(
        "Let me check.\n"
        "[AGENT_BUS_SEND to=ash] Hey Ash!\n"
        "Also contacting Nova.\n"
        "[AGENT_BUS_SEND to=nova] Hey Nova!\n"
        "All done.",
        out, sizeof(out), "kai", "/tmp/kai.sock");

    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_NULL(strstr(out, "AGENT_BUS_SEND"));

    cleanup();
}

/* Unknown peer — note says not found */
static void test_directive_unknown_peer(void)
{
    write_reg("ash=/tmp/ash\n");
    peer_registry_init(g_regfile, "kai");

    char out[1024];
    int count = bus_directive_process(
        "[AGENT_BUS_SEND to=unknown] Hello?",
        out, sizeof(out), "kai", "/tmp/kai.sock");

    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_NOT_NULL(strstr(out, "not found"));

    cleanup();
}

/* Offline peer — note says offline */
static void test_directive_offline_peer(void)
{
    write_reg("ash=/nonexistent/path\n");
    peer_registry_init(g_regfile, "kai");
    /* ash is registered but is_alive=0 (not probed/unreachable) */

    char out[1024];
    int count = bus_directive_process(
        "[AGENT_BUS_SEND to=ash] Hello!",
        out, sizeof(out), "kai", "/tmp/kai.sock");

    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_NOT_NULL(strstr(out, "offline"));

    cleanup();
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void test_bus_directive_suite(void)
{
    RUN_TEST(test_directive_no_directives);
    RUN_TEST(test_directive_single_send);
    RUN_TEST(test_directive_multiple_sends);
    RUN_TEST(test_directive_unknown_peer);
    RUN_TEST(test_directive_offline_peer);
}
