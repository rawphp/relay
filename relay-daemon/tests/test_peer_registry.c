#include "Unity/unity.h"
#include "peer_registry.h"
#include "relay.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_addir[256];

static void setup_addir(void)
{
    snprintf(g_addir, sizeof(g_addir),
             TEST_TMP_DIR "/peer_reg_%d", (int)getpid());
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

static void cleanup_addir(void)
{
    peer_registry_destroy();
    /* Remove all .json files */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -f %s/*.json", g_addir);
    (void)system(cmd);
    rmdir(g_addir);
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/* Happy path: scan directory with one peer (using own PID so it passes liveness) */
static void test_peer_registry_happy_path(void)
{
    setup_addir();
    /* Use our own PID so the liveness check passes */
    write_ad("ash", "/tmp/ash/data/relay.sock", (int)getpid());

    int rc = peer_registry_init(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());

    const peer_entry_t *p = peer_registry_get(0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("ash", p->name);
    TEST_ASSERT_EQUAL_STRING("/tmp/ash/data/relay.sock", p->socket_path);
    TEST_ASSERT_EQUAL_INT((int)getpid(), (int)p->pid);

    cleanup_addir();
}

/* Missing directory returns OK with 0 peers */
static void test_peer_registry_missing_dir(void)
{
    int rc = peer_registry_init("/nonexistent/relay.d", "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    peer_registry_destroy();
}

/* Empty directory returns OK with 0 peers */
static void test_peer_registry_empty_dir(void)
{
    setup_addir();

    int rc = peer_registry_init(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    cleanup_addir();
}

/* Self-exclusion: own name is not in peer list */
static void test_peer_registry_self_exclusion(void)
{
    setup_addir();
    write_ad("kai", "/tmp/kai/relay.sock", (int)getpid());

    int rc = peer_registry_init(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    cleanup_addir();
}

/* Stale entry (dead PID) is cleaned up */
static void test_peer_registry_stale_pid_cleanup(void)
{
    setup_addir();
    /* PID 99999999 is almost certainly dead */
    write_ad("ghost", "/tmp/ghost/relay.sock", 99999999);

    int rc = peer_registry_init(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    /* The stale file should have been deleted */
    char path[512];
    snprintf(path, sizeof(path), "%s/ghost.json", g_addir);
    TEST_ASSERT_NOT_EQUAL(0, access(path, F_OK));

    cleanup_addir();
}

/* Malformed JSON is skipped */
static void test_peer_registry_malformed_json(void)
{
    setup_addir();
    char path[512];
    snprintf(path, sizeof(path), "%s/bad.json", g_addir);
    FILE *f = fopen(path, "w");
    if (f) { fputs("not valid json", f); fclose(f); }

    write_ad("ash", "/tmp/ash/relay.sock", (int)getpid());

    int rc = peer_registry_init(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());

    cleanup_addir();
}

/* Non-JSON files are ignored */
static void test_peer_registry_non_json_ignored(void)
{
    setup_addir();
    char path[512];
    snprintf(path, sizeof(path), "%s/readme.txt", g_addir);
    FILE *f = fopen(path, "w");
    if (f) { fputs("hello", f); fclose(f); }

    int rc = peer_registry_init(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    unlink(path);
    cleanup_addir();
}

/* Multiple peers */
static void test_peer_registry_multiple_peers(void)
{
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    write_ad("nova", "/tmp/nova.sock", (int)getpid());
    write_ad("kai", "/tmp/kai.sock", (int)getpid());

    peer_registry_init(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(2, peer_registry_count());

    /* Both ash and nova should be present, not kai */
    int found_ash = 0, found_nova = 0;
    for (int i = 0; i < peer_registry_count(); i++) {
        if (strcmp(peer_registry_get(i)->name, "ash") == 0) found_ash = 1;
        if (strcmp(peer_registry_get(i)->name, "nova") == 0) found_nova = 1;
    }
    TEST_ASSERT_EQUAL_INT(1, found_ash);
    TEST_ASSERT_EQUAL_INT(1, found_nova);

    cleanup_addir();
}

/* get with out-of-bounds index returns NULL */
static void test_peer_registry_get_out_of_bounds(void)
{
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

    TEST_ASSERT_NULL(peer_registry_get(-1));
    TEST_ASSERT_NULL(peer_registry_get(1));
    TEST_ASSERT_NULL(peer_registry_get(100));

    cleanup_addir();
}

/* ── Probe tests ──────────────────────────────────────────────────────────── */

/* Helper: create a listening Unix socket, return fd */
static int create_listening_socket(const char *path)
{
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 1) < 0) {
        close(fd);
        unlink(path);
        return -1;
    }
    return fd;
}

/* Probe returns 1 for a peer with live PID and socket */
static void test_peer_probe_alive(void)
{
    char sockpath[256];
    snprintf(sockpath, sizeof(sockpath),
             TEST_TMP_DIR "/probe_alive2_%d.sock", (int)getpid());

    int listen_fd = create_listening_socket(sockpath);
    TEST_ASSERT_TRUE(listen_fd >= 0);

    setup_addir();
    write_ad("ash", sockpath, (int)getpid());
    peer_registry_init(g_addir, "kai");

    int alive = peer_registry_probe(0);
    TEST_ASSERT_EQUAL_INT(1, alive);
    TEST_ASSERT_EQUAL_INT(1, peer_registry_get(0)->is_alive);

    close(listen_fd);
    unlink(sockpath);
    cleanup_addir();
}

/* Probe with out-of-bounds index returns 0 */
static void test_peer_probe_out_of_bounds(void)
{
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

    TEST_ASSERT_EQUAL_INT(0, peer_registry_probe(-1));
    TEST_ASSERT_EQUAL_INT(0, peer_registry_probe(5));

    cleanup_addir();
}

/* ── Context builder tests ────────────────────────────────────────────────── */

/* build_context with peers produces the expected block */
static void test_peer_context_with_peers(void)
{
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    write_ad("nova", "/tmp/nova.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

    char buf[2048];
    int len = peer_registry_build_context(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Peer Agents"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "AGENT_BUS_SEND"));

    cleanup_addir();
}

/* build_context with no peers returns 0 */
static void test_peer_context_no_peers(void)
{
    setup_addir();
    peer_registry_init(g_addir, "kai");

    char buf[2048];
    int len = peer_registry_build_context(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, len);

    cleanup_addir();
}

/* find returns the correct peer */
static void test_peer_find_existing(void)
{
    setup_addir();
    write_ad("ash", "/tmp/ash.sock", (int)getpid());
    write_ad("nova", "/tmp/nova.sock", (int)getpid());
    peer_registry_init(g_addir, "kai");

    const peer_entry_t *p = peer_registry_find("nova");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("nova", p->name);

    TEST_ASSERT_NULL(peer_registry_find("unknown"));
    TEST_ASSERT_NULL(peer_registry_find(NULL));

    cleanup_addir();
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void test_peer_registry_suite(void)
{
    RUN_TEST(test_peer_registry_happy_path);
    RUN_TEST(test_peer_registry_missing_dir);
    RUN_TEST(test_peer_registry_empty_dir);
    RUN_TEST(test_peer_registry_self_exclusion);
    RUN_TEST(test_peer_registry_stale_pid_cleanup);
    RUN_TEST(test_peer_registry_malformed_json);
    RUN_TEST(test_peer_registry_non_json_ignored);
    RUN_TEST(test_peer_registry_multiple_peers);
    RUN_TEST(test_peer_registry_get_out_of_bounds);
    RUN_TEST(test_peer_probe_alive);
    RUN_TEST(test_peer_probe_out_of_bounds);
    RUN_TEST(test_peer_context_with_peers);
    RUN_TEST(test_peer_context_no_peers);
    RUN_TEST(test_peer_find_existing);
}
