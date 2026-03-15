#include "Unity/unity.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/*
 * These tests verify that unsetenv("CLAUDECODE") correctly removes the env
 * var from a child process's environment before exec, preventing the
 * "Claude Code cannot be launched inside another Claude Code session" error.
 */

static void test_unsetenv_removes_claudecode(void)
{
    /* Set CLAUDECODE in this process */
    int rc = setenv("CLAUDECODE", "1", 1);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(getenv("CLAUDECODE"));

    /* Unset it */
    unsetenv("CLAUDECODE");

    /* Verify gone */
    TEST_ASSERT_NULL(getenv("CLAUDECODE"));
}

static void test_child_does_not_inherit_unset_claudecode(void)
{
    /* Set CLAUDECODE in parent */
    setenv("CLAUDECODE", "1", 1);
    TEST_ASSERT_NOT_NULL(getenv("CLAUDECODE"));

    int pipefd[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(pipefd));

    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);

    if (pid == 0) {
        /* Child: unset CLAUDECODE (simulating what proc_spawn does) */
        unsetenv("CLAUDECODE");
        close(pipefd[0]);

        /* Write 1 if still set, 0 if gone */
        const char *val = getenv("CLAUDECODE");
        char result = (val != NULL) ? '1' : '0';
        ssize_t wr __attribute__((unused)) = write(pipefd[1], &result, 1);
        close(pipefd[1]);
        _exit(0);
    }

    /* Parent */
    close(pipefd[1]);
    char result = '?';
    ssize_t rd __attribute__((unused)) = read(pipefd[0], &result, 1);
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    /* Child must have seen CLAUDECODE as absent */
    TEST_ASSERT_EQUAL_CHAR('0', result);

    /* Clean up parent env */
    unsetenv("CLAUDECODE");
}

static void test_parent_env_unaffected_by_child_unset(void)
{
    /* Set CLAUDECODE in parent */
    setenv("CLAUDECODE", "session-xyz", 1);

    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);

    if (pid == 0) {
        /* Child: unset CLAUDECODE */
        unsetenv("CLAUDECODE");
        _exit(0);
    }

    int status;
    waitpid(pid, &status, 0);

    /* Parent's CLAUDECODE is unaffected by child's unset */
    const char *val = getenv("CLAUDECODE");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("session-xyz", val);

    /* Clean up */
    unsetenv("CLAUDECODE");
}

/* ── Suite ────────────────────────────────────────────────────────────────── */

void test_claudecode_env_suite(void)
{
    RUN_TEST(test_unsetenv_removes_claudecode);
    RUN_TEST(test_child_does_not_inherit_unset_claudecode);
    RUN_TEST(test_parent_env_unaffected_by_child_unset);
}
