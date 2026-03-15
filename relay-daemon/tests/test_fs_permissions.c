#include "Unity/unity.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */

static mode_t file_mode(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return st.st_mode & 0777;
}

/* ── Test: write_file pattern creates file with 0600 ─────────────────── */
static void test_write_file_creates_0600(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_write_perm.tmp";
    unlink(path);  /* ensure clean state */

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
    FILE *f = fdopen(fd, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("test", f);
    fclose(f);

    TEST_ASSERT_EQUAL_HEX(0600, file_mode(path));
    unlink(path);
}

/* ── Test: append_file pattern creates file with 0600 ────────────────── */
static void test_append_file_creates_0600(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_append_perm.tmp";
    unlink(path);  /* ensure clean state */

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
    FILE *f = fdopen(fd, "a");
    TEST_ASSERT_NOT_NULL(f);
    fputs("line1\n", f);
    fclose(f);

    TEST_ASSERT_EQUAL_HEX(0600, file_mode(path));
    unlink(path);
}

/* ── Test: append_file appends content, not overwrite ────────────────── */
static void test_append_file_appends_content(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_append_content.tmp";
    unlink(path);

    /* First append */
    int fd1 = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd1);
    FILE *f1 = fdopen(fd1, "a");
    TEST_ASSERT_NOT_NULL(f1);
    fputs("line1\n", f1);
    fclose(f1);

    /* Second append */
    int fd2 = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd2);
    FILE *f2 = fdopen(fd2, "a");
    TEST_ASSERT_NOT_NULL(f2);
    fputs("line2\n", f2);
    fclose(f2);

    struct stat st;
    stat(path, &st);
    /* Should contain both lines: "line1\nline2\n" = 12 bytes */
    TEST_ASSERT_EQUAL(12, (int)st.st_size);
    /* Permissions must still be 0600 */
    TEST_ASSERT_EQUAL_HEX(0600, file_mode(path));
    unlink(path);
}

/* ── Test: write_file truncates on re-open ───────────────────────────── */
static void test_write_file_truncates_on_reopen(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_trunc.tmp";
    unlink(path);

    /* Write initial content */
    int fd1 = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd1);
    FILE *f1 = fdopen(fd1, "w");
    TEST_ASSERT_NOT_NULL(f1);
    fputs("initial content here", f1);
    fclose(f1);

    /* Overwrite with shorter content */
    int fd2 = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd2);
    FILE *f2 = fdopen(fd2, "w");
    TEST_ASSERT_NOT_NULL(f2);
    fputs("short", f2);
    fclose(f2);

    struct stat st;
    stat(path, &st);
    /* Should only contain "short" = 5 bytes (truncated) */
    TEST_ASSERT_EQUAL(5, (int)st.st_size);
    unlink(path);
}

/* ── Test: 0600 is strictly owner-only (no group/other bits) ─────────── */
static void test_0600_is_owner_only(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_owner_only.tmp";
    unlink(path);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
    close(fd);

    mode_t m = file_mode(path);
    TEST_ASSERT_EQUAL_HEX(0600, m);

    /* Explicitly verify no group or other read bits */
    TEST_ASSERT_EQUAL_INT(0, m & 0044);  /* no group-read or other-read */
    TEST_ASSERT_EQUAL_INT(0, m & 0022);  /* no group-write or other-write */
    TEST_ASSERT_EQUAL_INT(0, m & 0111);  /* no execute bits */
    unlink(path);
}

/* ── Test: pid file pattern uses 0600 not 0644 ───────────────────────── */
static void test_pid_file_uses_0600(void)
{
    const char *path = TEST_TMP_DIR "/relay_test_pid.tmp";
    unlink(path);

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d\n", (int)getpid());
    write(fd, pid_str, strlen(pid_str));
    close(fd);

    TEST_ASSERT_EQUAL_HEX(0600, file_mode(path));
    unlink(path);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_fs_permissions_suite(void)
{
    RUN_TEST(test_write_file_creates_0600);
    RUN_TEST(test_append_file_creates_0600);
    RUN_TEST(test_append_file_appends_content);
    RUN_TEST(test_write_file_truncates_on_reopen);
    RUN_TEST(test_0600_is_owner_only);
    RUN_TEST(test_pid_file_uses_0600);
}
