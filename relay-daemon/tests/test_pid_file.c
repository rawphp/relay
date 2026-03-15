#include "Unity/unity.h"
#include "mocks.h"
#include "pid_file.h"
#include <string.h>
#include <stdlib.h>

/* ── Mock proc-liveness helpers ─────────────────────────────────────── */

static int mock_never_alive(int pid) { (void)pid; return 0; }
static int mock_always_alive(int pid) { (void)pid; return 1; }

static const pid_proc_t proc_never_alive  = { .is_alive = mock_never_alive  };
static const pid_proc_t proc_always_alive = { .is_alive = mock_always_alive };

/* ── Tests ───────────────────────────────────────────────────────────── */

/* No existing PID file — write new PID and return RELAY_OK. */
static void test_pid_no_file_writes_pid(void)
{
    mock_fs_reset();

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    int rc = pid_startup_check("/tmp/relay.pid", 12345,
                               &proc_never_alive, &g_mock_fs, log);
    log_close(log);

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    char *content = g_mock_fs.read_file("/tmp/relay.pid");
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "12345"));
    free(content);
}

/* Stale PID file (dead process) — auto-cleaned, new PID written. */
static void test_pid_stale_file_replaced(void)
{
    mock_fs_reset();
    mock_fs_set("/tmp/relay.pid", "99999\n");

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    int rc = pid_startup_check("/tmp/relay.pid", 12345,
                               &proc_never_alive, &g_mock_fs, log);
    log_close(log);

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    char *content = g_mock_fs.read_file("/tmp/relay.pid");
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "12345"));
    free(content);
}

/* Live PID file — return RELAY_ERR_FULL, do NOT overwrite. */
static void test_pid_live_process_rejects(void)
{
    mock_fs_reset();
    mock_fs_set("/tmp/relay.pid", "99999\n");

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    int rc = pid_startup_check("/tmp/relay.pid", 12345,
                               &proc_always_alive, &g_mock_fs, log);
    log_close(log);

    TEST_ASSERT_EQUAL_INT(RELAY_ERR_FULL, rc);
    /* Original content must not be overwritten */
    char *content = g_mock_fs.read_file("/tmp/relay.pid");
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "99999"));
    free(content);
}

/* pid_file_remove deletes the file. */
static void test_pid_remove_deletes_file(void)
{
    mock_fs_reset();
    mock_fs_set("/tmp/relay.pid", "12345\n");

    pid_file_remove("/tmp/relay.pid", &g_mock_fs);

    TEST_ASSERT_EQUAL_INT(0, g_mock_fs.file_exists("/tmp/relay.pid"));
}

/* ── Test: pid_default_path derives from config path ─────────────────── */

static void test_pid_default_path_from_config(void)
{
    char out[256];
    int rc = pid_default_path("/home/agent/relay/config/relay.conf",
                              out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("/home/agent/relay/data/relay.pid", out);
}

static void test_pid_default_path_null_config(void)
{
    char out[256];
    int rc = pid_default_path(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
}

static void test_pid_default_path_no_config_suffix(void)
{
    /* If config path doesn't end with /config/relay.conf, fallback */
    char out[256];
    int rc = pid_default_path("/some/random/path.conf", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
}

/* ── Suite ───────────────────────────────────────────────────────────── */

void test_pid_file_suite(void)
{
    RUN_TEST(test_pid_no_file_writes_pid);
    RUN_TEST(test_pid_stale_file_replaced);
    RUN_TEST(test_pid_live_process_rejects);
    RUN_TEST(test_pid_remove_deletes_file);
    RUN_TEST(test_pid_default_path_from_config);
    RUN_TEST(test_pid_default_path_null_config);
    RUN_TEST(test_pid_default_path_no_config_suffix);
}
