#include "Unity/unity.h"
#include "peer_registry.h"
#include "relay.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_regfile[256];

static void write_registry(const char *content)
{
    snprintf(g_regfile, sizeof(g_regfile),
             TEST_TMP_DIR "/test_peer_registry_%d.txt", (int)getpid());
    FILE *f = fopen(g_regfile, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static void cleanup_registry(void)
{
    peer_registry_destroy();
    if (g_regfile[0]) {
        unlink(g_regfile);
        g_regfile[0] = '\0';
    }
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/* Happy path: two agents in registry, self excluded */
static void test_peer_registry_happy_path(void)
{
    write_registry("kai=/Users/tom/kai\nash=/Users/tom/ash\n");

    int rc = peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());

    const peer_entry_t *p = peer_registry_get(0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("ash", p->name);
    TEST_ASSERT_EQUAL_STRING("/Users/tom/ash", p->home_path);
    TEST_ASSERT_EQUAL_STRING("/Users/tom/ash/data/relay.sock", p->socket_path);

    cleanup_registry();
}

/* Missing registry file returns OK with 0 peers */
static void test_peer_registry_missing_file(void)
{
    int rc = peer_registry_init("/nonexistent/path/to/registry", "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    peer_registry_destroy();
}

/* Empty registry file returns OK with 0 peers */
static void test_peer_registry_empty_file(void)
{
    write_registry("");

    int rc = peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    cleanup_registry();
}

/* Self-exclusion: own name is not in peer list */
static void test_peer_registry_self_exclusion(void)
{
    write_registry("kai=/Users/tom/kai\n");

    int rc = peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_count());

    cleanup_registry();
}

/* Malformed lines are skipped */
static void test_peer_registry_malformed_lines(void)
{
    write_registry("kai=/Users/tom/kai\n"
                   "this is garbage\n"
                   "=noname\n"
                   "nopath=\n"
                   "ash=/Users/tom/ash\n");

    int rc = peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());

    const peer_entry_t *p = peer_registry_get(0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("ash", p->name);

    cleanup_registry();
}

/* Comment lines (starting with #) are skipped */
static void test_peer_registry_comments_skipped(void)
{
    write_registry("# this is a comment\n"
                   "ash=/Users/tom/ash\n"
                   "  # indented comment\n"
                   "nova=/Users/tom/nova\n");

    int rc = peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(2, peer_registry_count());

    cleanup_registry();
}

/* Blank lines are skipped */
static void test_peer_registry_blank_lines(void)
{
    write_registry("\n\nash=/Users/tom/ash\n\n\n");

    int rc = peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());

    cleanup_registry();
}

/* Max peers exceeded — only first 16 are kept */
static void test_peer_registry_max_peers(void)
{
    char buf[4096];
    int off = 0;
    /* Write 18 entries (kai + 17 others) */
    off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                    "kai=/Users/tom/kai\n");
    for (int i = 0; i < 17; i++) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                        "agent%02d=/Users/tom/agent%02d\n", i, i);
    }
    write_registry(buf);

    int rc = peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    /* 17 others, but max is 16 */
    TEST_ASSERT_EQUAL_INT(PEER_REGISTRY_MAX, peer_registry_count());

    cleanup_registry();
}

/* get with out-of-bounds index returns NULL */
static void test_peer_registry_get_out_of_bounds(void)
{
    write_registry("ash=/Users/tom/ash\n");

    peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_NULL(peer_registry_get(-1));
    TEST_ASSERT_NULL(peer_registry_get(1));
    TEST_ASSERT_NULL(peer_registry_get(100));

    cleanup_registry();
}

/* Multiple peers — ordering preserved */
static void test_peer_registry_multiple_peers(void)
{
    write_registry("ash=/Users/tom/ash\n"
                   "nova=/Users/tom/nova\n"
                   "kai=/Users/tom/kai\n"
                   "echo=/Users/tom/echo\n");

    peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(3, peer_registry_count());

    TEST_ASSERT_EQUAL_STRING("ash", peer_registry_get(0)->name);
    TEST_ASSERT_EQUAL_STRING("nova", peer_registry_get(1)->name);
    TEST_ASSERT_EQUAL_STRING("echo", peer_registry_get(2)->name);

    cleanup_registry();
}

/* Tilde in home path is stored as-is (not expanded) */
static void test_peer_registry_tilde_path(void)
{
    write_registry("ash=~/ash\n");

    peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());
    TEST_ASSERT_EQUAL_STRING("~/ash", peer_registry_get(0)->home_path);
    TEST_ASSERT_EQUAL_STRING("~/ash/data/relay.sock", peer_registry_get(0)->socket_path);

    cleanup_registry();
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

/* Probe returns 1 for a peer with a live socket */
static void test_peer_probe_alive(void)
{
    /* Create a socket that the peer "ash" would listen on */
    char sockpath[256];
    snprintf(sockpath, sizeof(sockpath),
             TEST_TMP_DIR "/probe_alive_%d/data/relay.sock", (int)getpid());

    /* Create the directory structure */
    char dir1[256], dir2[256];
    snprintf(dir1, sizeof(dir1), TEST_TMP_DIR "/probe_alive_%d", (int)getpid());
    snprintf(dir2, sizeof(dir2), TEST_TMP_DIR "/probe_alive_%d/data", (int)getpid());
    mkdir(dir1, 0755);
    mkdir(dir2, 0755);

    int listen_fd = create_listening_socket(sockpath);
    TEST_ASSERT_TRUE(listen_fd >= 0);

    /* Register ash pointing to this temp directory */
    char regcontent[512];
    snprintf(regcontent, sizeof(regcontent), "ash=%s\n", dir1);
    write_registry(regcontent);
    peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());

    /* Probe should succeed */
    int alive = peer_registry_probe(0);
    TEST_ASSERT_EQUAL_INT(1, alive);
    TEST_ASSERT_EQUAL_INT(1, peer_registry_get(0)->is_alive);

    close(listen_fd);
    unlink(sockpath);
    rmdir(dir2);
    rmdir(dir1);
    cleanup_registry();
}

/* Probe returns 0 for a peer whose socket doesn't exist */
static void test_peer_probe_dead(void)
{
    write_registry("ash=/nonexistent/agent/path\n");
    peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(1, peer_registry_count());

    int alive = peer_registry_probe(0);
    TEST_ASSERT_EQUAL_INT(0, alive);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_get(0)->is_alive);

    cleanup_registry();
}

/* Probe with out-of-bounds index returns 0 */
static void test_peer_probe_out_of_bounds(void)
{
    write_registry("ash=/Users/tom/ash\n");
    peer_registry_init(g_regfile, "kai");

    TEST_ASSERT_EQUAL_INT(0, peer_registry_probe(-1));
    TEST_ASSERT_EQUAL_INT(0, peer_registry_probe(5));

    cleanup_registry();
}

/* probe_all updates is_alive on all entries */
static void test_peer_probe_all(void)
{
    write_registry("ash=/nonexistent/one\nnova=/nonexistent/two\n");
    peer_registry_init(g_regfile, "kai");
    TEST_ASSERT_EQUAL_INT(2, peer_registry_count());

    peer_registry_probe_all();
    TEST_ASSERT_EQUAL_INT(0, peer_registry_get(0)->is_alive);
    TEST_ASSERT_EQUAL_INT(0, peer_registry_get(1)->is_alive);

    cleanup_registry();
}

/* ── Context builder tests ────────────────────────────────────────────────── */

/* build_context with peers produces the expected block */
static void test_peer_context_with_peers(void)
{
    write_registry("ash=/Users/tom/ash\nnova=/Users/tom/nova\n");
    peer_registry_init(g_regfile, "kai");

    /* Mark ash alive, nova dead */
    /* We can't easily set is_alive without probing, so we'll just test
       that the context block is generated with offline status */
    char buf[2048];
    int len = peer_registry_build_context(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Peer Agents"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "ash"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "nova"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "AGENT_BUS_SEND"));

    cleanup_registry();
}

/* build_context with no peers returns 0 */
static void test_peer_context_no_peers(void)
{
    write_registry("kai=/Users/tom/kai\n");
    peer_registry_init(g_regfile, "kai");

    char buf[2048];
    int len = peer_registry_build_context(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, len);
    TEST_ASSERT_EQUAL_STRING("", buf);

    cleanup_registry();
}

/* build_context shows online/offline status */
static void test_peer_context_shows_status(void)
{
    write_registry("ash=/nonexistent/path\n");
    peer_registry_init(g_regfile, "kai");

    char buf[2048];
    peer_registry_build_context(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "offline"));

    cleanup_registry();
}

/* find returns the correct peer */
static void test_peer_find_existing(void)
{
    write_registry("ash=/Users/tom/ash\nnova=/Users/tom/nova\n");
    peer_registry_init(g_regfile, "kai");

    const peer_entry_t *p = peer_registry_find("nova");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("nova", p->name);

    TEST_ASSERT_NULL(peer_registry_find("unknown"));
    TEST_ASSERT_NULL(peer_registry_find(NULL));

    cleanup_registry();
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void test_peer_registry_suite(void)
{
    RUN_TEST(test_peer_registry_happy_path);
    RUN_TEST(test_peer_registry_missing_file);
    RUN_TEST(test_peer_registry_empty_file);
    RUN_TEST(test_peer_registry_self_exclusion);
    RUN_TEST(test_peer_registry_malformed_lines);
    RUN_TEST(test_peer_registry_comments_skipped);
    RUN_TEST(test_peer_registry_blank_lines);
    RUN_TEST(test_peer_registry_max_peers);
    RUN_TEST(test_peer_registry_get_out_of_bounds);
    RUN_TEST(test_peer_registry_multiple_peers);
    RUN_TEST(test_peer_registry_tilde_path);
    RUN_TEST(test_peer_probe_alive);
    RUN_TEST(test_peer_probe_dead);
    RUN_TEST(test_peer_probe_out_of_bounds);
    RUN_TEST(test_peer_probe_all);
    RUN_TEST(test_peer_context_with_peers);
    RUN_TEST(test_peer_context_no_peers);
    RUN_TEST(test_peer_context_shows_status);
    RUN_TEST(test_peer_find_existing);
}
