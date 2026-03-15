#include "Unity/unity.h"
#include "peer_registry.h"
#include "relay.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
}
