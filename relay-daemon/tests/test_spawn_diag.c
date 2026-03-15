#include "Unity/unity.h"
#include "el_reply.h"
#include <string.h>
#include <stdlib.h>

/* ── Tests: el_spawn_diag_text ─────────────────────────────────────── */

static void test_spawn_diag_includes_env_vars(void)
{
    char buf[1024];
    el_spawn_diag_text(buf, sizeof(buf));
    /* Must include the three diagnostic env var names */
    TEST_ASSERT_NOT_NULL(strstr(buf, "CLAUDECODE="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "HOME="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "PATH="));
}

static void test_spawn_diag_truncates_path(void)
{
    char buf[1024];
    el_spawn_diag_text(buf, sizeof(buf));
    /* PATH value in output must be <= 200 chars (truncated) */
    const char *path_start = strstr(buf, "PATH=");
    TEST_ASSERT_NOT_NULL(path_start);
    path_start += 5; /* skip "PATH=" */
    /* Find end of PATH value (next space or end of string) */
    const char *path_end = strchr(path_start, ' ');
    if (!path_end) path_end = path_start + strlen(path_start);
    int path_len = (int)(path_end - path_start);
    TEST_ASSERT_TRUE(path_len <= 203); /* 200 + "..." */
}

static void test_spawn_diag_shows_unset_for_missing_var(void)
{
    /* CLAUDECODE should not be set in test environment */
    char *saved = NULL;
    char *existing = getenv("CLAUDECODE");
    if (existing) {
        saved = strdup(existing);
        unsetenv("CLAUDECODE");
    }

    char buf[1024];
    el_spawn_diag_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "CLAUDECODE=(unset)"));

    /* Restore if it was set */
    if (saved) {
        setenv("CLAUDECODE", saved, 1);
        free(saved);
    }
}

static void test_spawn_diag_null_home(void)
{
    char *saved = NULL;
    char *existing = getenv("HOME");
    if (existing) {
        saved = strdup(existing);
        unsetenv("HOME");
    }

    char buf[1024];
    el_spawn_diag_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "HOME=(unset)"));

    if (saved) {
        setenv("HOME", saved, 1);
        free(saved);
    }
}

void test_spawn_diag_suite(void)
{
    RUN_TEST(test_spawn_diag_includes_env_vars);
    RUN_TEST(test_spawn_diag_truncates_path);
    RUN_TEST(test_spawn_diag_shows_unset_for_missing_var);
    RUN_TEST(test_spawn_diag_null_home);
}
