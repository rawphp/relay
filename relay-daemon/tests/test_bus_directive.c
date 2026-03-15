#include "Unity/unity.h"
#include "bus_directive.h"
#include "peer_registry.h"
#include "relay.h"

#include <cJSON/cJSON.h>
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
             TEST_TMP_DIR "/bus_dir_%d", (int)getpid());
    mkdir(g_addir, 0700);
}

static void write_ad(const char *name, const char *socket, int pid)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", g_addir, name);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", name);
    cJSON_AddNumberToObject(obj, "pid", pid);
    cJSON_AddStringToObject(obj, "socket", socket);
    cJSON_AddNumberToObject(obj, "started", 1710500000);
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    FILE *f = fopen(path, "w");
    if (f) { fputs(json, f); fclose(f); }
    free(json);
}

static void cleanup(void)
{
    peer_registry_destroy();
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_addir);
    system(cmd);
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/* No directives — text passes through unchanged */
static void test_directive_no_directives(void)
{
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

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
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

    char out[1024];
    int count = bus_directive_process(
        "Sure!\n[AGENT_BUS_SEND to=ash] Hey Ash, how are you?\nDone.",
        out, sizeof(out), "kai", "/tmp/kai.sock");

    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_NULL(strstr(out, "AGENT_BUS_SEND"));
    TEST_ASSERT_NOT_NULL(strstr(out, "ash"));

    cleanup();
}

/* Multiple directives are all processed */
static void test_directive_multiple_sends(void)
{
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    write_ad("nova", "/tmp/nova.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

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
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

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
    setup_addir();
    /* Create peer with alive PID but mark as offline by probing
       a non-existent socket after init */
    write_ad("ash", "/nonexistent/relay.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

    /* Probe to set is_alive=0 (socket doesn't exist) */
    peer_registry_probe(0);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_get(0)->is_alive);

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
