#include "Unity/unity.h"
#include "group_chat_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Create {dir}/data/ and write content to {dir}/data/recent_group_chat.txt. */
static void write_ctx_file(const char *dir, const char *content)
{
    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s/data", dir);
    mkdir(data_dir, 0755);

    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", dir);
    FILE *fp = fopen(path, "w");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

/* Set mtime on {dir}/data/recent_group_chat.txt to `seconds_ago` seconds ago. */
static void set_mtime_ago(const char *dir, int seconds_ago)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", dir);
    struct utimbuf tb;
    tb.actime  = time(NULL) - seconds_ago;
    tb.modtime = time(NULL) - seconds_ago;
    utime(path, &tb);
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

static void test_returns_zero_when_file_missing(void)
{
    char buf[256] = "unchanged";
    int rc = group_chat_context_load("/nonexistent/path/that/does/not/exist", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("unchanged", buf); /* buf untouched */
}

static void test_returns_one_when_file_exists_and_recent(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    const char *expected = "## Recent Group Chat\n\n[human]: hello\n[agent]: hi\n";
    write_ctx_file(tmpdir, expected);

    char buf[512] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

static void test_returns_zero_when_file_older_than_24h(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    write_ctx_file(tmpdir, "## Recent Group Chat\n\n[human]: old\n");
    set_mtime_ago(tmpdir, 25 * 3600); /* 25 hours ago */

    char buf[256] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(0, rc);

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

static void test_truncates_content_to_buf_len(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    /* Write 200 bytes of content */
    char big[201];
    memset(big, 'A', 200);
    big[200] = '\0';
    write_ctx_file(tmpdir, big);

    char buf[64] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf)); /* buf_len=64 */

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_INT(63, (int)strlen(buf)); /* max buf_len-1 bytes */

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

static void test_empty_file_returns_zero(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    write_ctx_file(tmpdir, ""); /* empty content */

    char buf[256] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(0, rc);

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

/* ── Session-dir helpers ──────────────────────────────────────────────────── */

/* Create {dir}/data/sessions/ and write a .txt file inside it. */
static void write_session_file(const char *dir, const char *filename,
                               const char *content)
{
    char sessions_dir[512];
    snprintf(sessions_dir, sizeof(sessions_dir), "%s/data/sessions", dir);
    mkdir(sessions_dir, 0755); /* parent data/ already exists from write_ctx_file */

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", sessions_dir, filename);
    FILE *fp = fopen(path, "w");
    if (fp) { fputs(content, fp); fclose(fp); }
}

static void set_session_mtime_ago(const char *dir, const char *filename, int seconds_ago)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/data/sessions/%s", dir, filename);
    struct utimbuf tb;
    tb.actime  = time(NULL) - seconds_ago;
    tb.modtime = time(NULL) - seconds_ago;
    utime(path, &tb);
}

/* ── Session-dir tests ────────────────────────────────────────────────────── */

static void test_loads_from_sessions_dir_when_present(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    /* Create data/ so write_session_file can make data/sessions/ */
    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s/data", tmpdir);
    mkdir(data_dir, 0755);

    const char *session_content = "## Recent Group Chat\n\n[nova]: hi from session\n";
    write_session_file(tmpdir, "abc12345.txt", session_content);

    char buf[512] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_STRING(session_content, buf);

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/sessions/abc12345.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data/sessions", tmpdir);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

static void test_falls_back_to_global_when_no_sessions_dir(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    const char *global_content = "## Recent Group Chat\n\n[human]: global\n";
    write_ctx_file(tmpdir, global_content);

    char buf[512] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_STRING(global_content, buf);

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

static void test_prefers_most_recent_session_file(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s/data", tmpdir);
    mkdir(data_dir, 0755);

    write_session_file(tmpdir, "old_sess.txt", "## Recent Group Chat\n\n[agent]: old\n");
    set_session_mtime_ago(tmpdir, "old_sess.txt", 3600); /* 1 hour ago */

    write_session_file(tmpdir, "new_sess.txt", "## Recent Group Chat\n\n[nova]: new\n");
    /* new_sess.txt has current mtime (most recent) */

    char buf[512] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_NOT_NULL(strstr(buf, "[nova]: new"));

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/sessions/old_sess.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data/sessions/new_sess.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data/sessions", tmpdir);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

static void test_session_file_older_than_24h_falls_back_to_global(void)
{
    char tmpdir[] = TEST_TMP_DIR "/relay_ctx_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s/data", tmpdir);
    mkdir(data_dir, 0755);

    /* Session file is stale (25h old) */
    write_session_file(tmpdir, "stale.txt", "## Recent Group Chat\n\n[nova]: stale\n");
    set_session_mtime_ago(tmpdir, "stale.txt", 25 * 3600);

    /* Global file is recent */
    write_ctx_file(tmpdir, "## Recent Group Chat\n\n[human]: global fallback\n");

    char buf[512] = "";
    int rc = group_chat_context_load(tmpdir, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_NOT_NULL(strstr(buf, "global fallback"));

    /* cleanup */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/sessions/stale.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data/sessions", tmpdir);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", tmpdir);
    remove(path);
    snprintf(path, sizeof(path), "%s/data", tmpdir);
    rmdir(path);
    rmdir(tmpdir);
}

/* ── Suite registration ───────────────────────────────────────────────────── */

void group_chat_context_suite(void)
{
    RUN_TEST(test_returns_zero_when_file_missing);
    RUN_TEST(test_returns_one_when_file_exists_and_recent);
    RUN_TEST(test_returns_zero_when_file_older_than_24h);
    RUN_TEST(test_truncates_content_to_buf_len);
    RUN_TEST(test_empty_file_returns_zero);
    /* Session-dir tests */
    RUN_TEST(test_loads_from_sessions_dir_when_present);
    RUN_TEST(test_falls_back_to_global_when_no_sessions_dir);
    RUN_TEST(test_prefers_most_recent_session_file);
    RUN_TEST(test_session_file_older_than_24h_falls_back_to_global);
}
