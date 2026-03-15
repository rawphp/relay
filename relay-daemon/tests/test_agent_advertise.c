#include "Unity/unity.h"
#include "agent_advertise.h"
#include "relay.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static char g_addir[256];

static void setup_addir(void)
{
    snprintf(g_addir, sizeof(g_addir),
             TEST_TMP_DIR "/relay_ad_%d", (int)getpid());
    /* Don't create — let publish do it */
}

static void cleanup_addir(void)
{
    char path[512];
    /* Remove any JSON files */
    snprintf(path, sizeof(path), "%s/kai.json", g_addir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/ash.json", g_addir);
    unlink(path);
    rmdir(g_addir);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    static char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/* publish creates the directory and writes a valid JSON file */
static void test_advertise_publish_creates_file(void)
{
    setup_addir();

    int rc = agent_advertise_publish(g_addir, "kai", "/tmp/kai/data/relay.sock");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* File should exist */
    char path[512];
    snprintf(path, sizeof(path), "%s/kai.json", g_addir);
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));

    /* Parse and validate JSON */
    char *content = read_file(path);
    TEST_ASSERT_NOT_NULL(content);

    cJSON *root = cJSON_Parse(content);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *name = cJSON_GetObjectItem(root, "name");
    TEST_ASSERT_TRUE(cJSON_IsString(name));
    TEST_ASSERT_EQUAL_STRING("kai", name->valuestring);

    cJSON *pid = cJSON_GetObjectItem(root, "pid");
    TEST_ASSERT_TRUE(cJSON_IsNumber(pid));
    TEST_ASSERT_EQUAL_INT((int)getpid(), (int)pid->valuedouble);

    cJSON *sock = cJSON_GetObjectItem(root, "socket");
    TEST_ASSERT_TRUE(cJSON_IsString(sock));
    TEST_ASSERT_EQUAL_STRING("/tmp/kai/data/relay.sock", sock->valuestring);

    cJSON *started = cJSON_GetObjectItem(root, "started");
    TEST_ASSERT_TRUE(cJSON_IsNumber(started));
    TEST_ASSERT_TRUE(started->valuedouble > 0);

    cJSON_Delete(root);
    cleanup_addir();
}

/* publish creates the directory if it doesn't exist */
static void test_advertise_publish_creates_directory(void)
{
    setup_addir();

    /* Directory should not exist yet */
    struct stat st;
    TEST_ASSERT_NOT_EQUAL(0, stat(g_addir, &st));

    int rc = agent_advertise_publish(g_addir, "kai", "/tmp/sock");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Directory should now exist */
    TEST_ASSERT_EQUAL_INT(0, stat(g_addir, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));

    cleanup_addir();
}

/* withdraw removes the file */
static void test_advertise_withdraw_removes_file(void)
{
    setup_addir();
    agent_advertise_publish(g_addir, "kai", "/tmp/sock");

    char path[512];
    snprintf(path, sizeof(path), "%s/kai.json", g_addir);
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));

    int rc = agent_advertise_withdraw(g_addir, "kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_NOT_EQUAL(0, access(path, F_OK));

    cleanup_addir();
}

/* withdraw on nonexistent file returns RELAY_ERR_NOTFOUND */
static void test_advertise_withdraw_missing(void)
{
    setup_addir();
    mkdir(g_addir, 0700);

    int rc = agent_advertise_withdraw(g_addir, "nonexistent");
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_NOTFOUND, rc);

    cleanup_addir();
}

/* publish overwrites an existing file (e.g. after restart) */
static void test_advertise_publish_overwrites(void)
{
    setup_addir();
    agent_advertise_publish(g_addir, "kai", "/tmp/old.sock");
    agent_advertise_publish(g_addir, "kai", "/tmp/new.sock");

    char path[512];
    snprintf(path, sizeof(path), "%s/kai.json", g_addir);
    char *content = read_file(path);
    TEST_ASSERT_NOT_NULL(strstr(content, "/tmp/new.sock"));

    cleanup_addir();
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void test_agent_advertise_suite(void)
{
    RUN_TEST(test_advertise_publish_creates_file);
    RUN_TEST(test_advertise_publish_creates_directory);
    RUN_TEST(test_advertise_withdraw_removes_file);
    RUN_TEST(test_advertise_withdraw_missing);
    RUN_TEST(test_advertise_publish_overwrites);
}
